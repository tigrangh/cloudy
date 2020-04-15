#include "libavwrapper.hpp"
#include "admin_model.hpp"

#include <mesh.pp/cryptoutility.hpp>

#include <boost/filesystem/path.hpp>

#define __STDC_CONSTANT_MACROS

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>
#include <libavutil/opt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
//#include <stdio.h>
//#include <stdarg.h>
//#include <stdlib.h>
//#include <string.h>
#include <inttypes.h>
}

#include <cassert>
#include <utility>

using std::string;
using beltpp::packet;
using std::vector;
using std::pair;
using std::unordered_map;

namespace libavwrapper
{

using packet_ptr = beltpp::t_unique_ptr<AVPacket>;
packet_ptr packet_null()
{
    return packet_ptr(nullptr, [](AVPacket* p)
    {
        if (nullptr != p)
            av_packet_free(&p);
    });
}
packet_ptr packet_alloc()
{
    auto res = packet_null();
    res.reset(av_packet_alloc());
    return res;
}
void packet_unref(packet_ptr& ptr)
{
    av_packet_unref(ptr.get());
}

using frame_ptr = beltpp::t_unique_ptr<AVFrame>;
frame_ptr frame_alloc()
{
    return frame_ptr(av_frame_alloc(), [](AVFrame* p)
    {
        if (nullptr != p)
            av_frame_free(&p);
    });
}
void frame_unref(frame_ptr& ptr)
{
    av_frame_unref(ptr.get());
}

using filter_graph_ptr = beltpp::t_unique_ptr<AVFilterGraph>;
filter_graph_ptr filter_graph_null()
{
    return filter_graph_ptr(nullptr, [](AVFilterGraph* p)
    {
        if (nullptr != p)
            avfilter_graph_free(&p);
    });
}
filter_graph_ptr filter_graph_alloc()
{
    auto res = filter_graph_null();
    res.reset(avfilter_graph_alloc());
    return res;
}

using codec_ptr = beltpp::t_unique_ptr<AVCodec>;
codec_ptr codec_null()
{
    return beltpp::t_unique_nullptr<AVCodec>();
}
codec_ptr codec_find_decoder(AVCodecID id)
{
    auto res = codec_null();
    res.reset(avcodec_find_decoder(id));
    return res;
}
codec_ptr codec_find_encoder(string const& name)
{
    auto res = codec_null();
    res.reset(avcodec_find_encoder_by_name(name.c_str()));
    return res;
}

using codec_context_ptr = beltpp::t_unique_ptr<AVCodecContext>;
codec_context_ptr codec_context_null()
{
    return codec_context_ptr(nullptr, [](AVCodecContext* p)
    {
        if (nullptr != p)
            avcodec_free_context(&p);
    });
}
codec_context_ptr codec_context_alloc(codec_ptr& avcodec)
{
    auto res = codec_context_null();
    res.reset(avcodec_alloc_context3(avcodec.get()));
    return res;
}

using format_context_ptr = beltpp::t_unique_ptr<AVFormatContext>;
format_context_ptr format_context_null()
{
    return format_context_ptr(nullptr, [](AVFormatContext* p)
    {
        if (nullptr != p)
            avformat_free_context(p);
    });
}

format_context_ptr format_context_alloc_output(string const& filepath)
{
    auto res = format_context_null();
    AVFormatContext* p = nullptr;
    avformat_alloc_output_context2(&p,
                                   nullptr,
                                   nullptr,
                                   filepath.c_str());

    res.reset(p);

    return res;
}
format_context_ptr format_context_alloc_input(string const& filepath)
{
    auto res = format_context_ptr(nullptr, [](AVFormatContext* p)
    {
        if (nullptr != p)
        {
            avformat_close_input(&p);
            avformat_free_context(p);
        }
    });

    AVFormatContext* p = avformat_alloc_context();

    if (nullptr != p &&
        0 == avformat_open_input(&p, filepath.c_str(), nullptr, nullptr) &&
        0 <= avformat_find_stream_info(p, nullptr))
        res.reset(p);

    return res;
}

using stream_ptr = beltpp::t_unique_ptr<AVStream>;
stream_ptr stream_null()
{
    return beltpp::t_unique_nullptr<AVStream>();
}
stream_ptr stream_raw(AVStream* p)
{
    auto res = beltpp::t_unique_nullptr<AVStream>();
    res.reset(p);
    return res;
}
stream_ptr format_new_stream(format_context_ptr& fc)
{
    auto res = beltpp::t_unique_nullptr<AVStream>();
    res.reset(avformat_new_stream(fc.get(), nullptr));
    return res;
}

class CodecContextDefinition
{
public:
    int index;
    codec_ptr avcodec;
    stream_ptr avstream;
    codec_context_ptr avcodec_context;
    AVMediaType avmedia_type;

    CodecContextDefinition()
        : avcodec(codec_null())
        , avstream(stream_null())
        , avcodec_context(codec_context_null())
        , avmedia_type(AVMEDIA_TYPE_UNKNOWN)
    {}

    bool fill_stream_info(AVStream& avstream_,
                          int index_)
    {
        avstream = stream_raw(&avstream_);
        index = index_;
        avmedia_type = avstream_.codecpar->codec_type;

        avcodec = codec_find_decoder(avstream_.codecpar->codec_id);
        if (nullptr == avcodec)
        {
            //logging("failed to find the codec");
            return false;
        }

        avcodec_context = codec_context_alloc(avcodec);
        if (nullptr == avcodec_context)
        {
            //logging("failed to alloc memory for codec context");
            return false;
        }

        if (avcodec_parameters_to_context(avcodec_context.get(), avstream_.codecpar) < 0)
        {
            //logging("failed to fill codec context");
            return false;
        }

        if (0 > avcodec_open2(avcodec_context.get(), avcodec.get(), nullptr))
        {
            //logging("failed to open codec");
            return false;
        }

        return true;
    }
};

class DecoderCodecContextDefinition : public CodecContextDefinition
{
public:
};

class EncoderCodecContextDefinition : public CodecContextDefinition
{
public:
    AdminModel::MediaTypeDescriptionAVStream options;

    AVFilterContext* filter_context_source = nullptr;
    AVFilterContext* filter_context_sink = nullptr;
    filter_graph_ptr filter_graph = filter_graph_null();

    // desired output stream properties
    int width = 0;
    int height = 0;
    AVRational frame_rate;
    int format = 0;
    int sample_rate = 0;
    uint64_t channel_layout = 0;

    // those are only set if no format is specified and the encoder gives us multiple options
    vector<AVRational> frame_rates;
    vector<int> formats;
    vector<int> sample_rates;
    vector<uint64_t> channel_layouts;

    bool create_avcodec(string const& codec_name)
    {
        assert(false == codec_name.empty());
        avcodec = codec_find_encoder(codec_name);
        if (nullptr == avcodec)
        {
            //logging("could not find the proper codec");
            return false;
        }

        return true;
    }

    void avcodec_context_init(AdminModel::MediaTypeDescriptionAVStreamTranscode const& options,
                              DecoderCodecContextDefinition const& decoder,
                              AVRational input_framerate)
    {
        if (decoder.avmedia_type == AVMEDIA_TYPE_AUDIO)
        {
            int sample_rate = decoder.avcodec_context->sample_rate;

            int OUTPUT_CHANNELS = 2;
            int OUTPUT_BIT_RATE = 196000;
            avcodec_context->channels       = OUTPUT_CHANNELS;
            avcodec_context->channel_layout = av_get_default_channel_layout(OUTPUT_CHANNELS);
            avcodec_context->sample_rate    = sample_rate;
            avcodec_context->sample_fmt     = avcodec->sample_fmts[0];
            avcodec_context->bit_rate       = OUTPUT_BIT_RATE;
            avcodec_context->time_base      = (AVRational){1, sample_rate};

            avcodec_context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

            avstream->time_base = avcodec_context->time_base;
        }
        else if (decoder.avmedia_type == AVMEDIA_TYPE_VIDEO)
        {
            av_opt_set(avcodec_context->priv_data, "preset", "fast", 0);
            if (false == options.codec_priv_key.empty() &&
                false == options.codec_priv_value.empty())
                av_opt_set(avcodec_context->priv_data,
                           options.codec_priv_key.c_str(),
                           options.codec_priv_value.c_str(), 0);

            avcodec_context->height = decoder.avcodec_context->height;
            avcodec_context->width = decoder.avcodec_context->width;
            avcodec_context->sample_aspect_ratio = decoder.avcodec_context->sample_aspect_ratio;
            if (avcodec->pix_fmts)
                avcodec_context->pix_fmt = avcodec->pix_fmts[0];
            else
                avcodec_context->pix_fmt = decoder.avcodec_context->pix_fmt;

            //avcodec_context->bit_rate = 2 * 1000 * 1000;
            avcodec_context->rc_buffer_size = 4 * 1000 * 1000;
            avcodec_context->rc_max_rate = 2 * 1000 * 1000;
            avcodec_context->rc_min_rate = 2.5 * 1000 * 1000;

            avcodec_context->time_base = av_inv_q(input_framerate);
            avstream->time_base = avcodec_context->time_base;
            //
            avcodec_context->width = 800;
            avcodec_context->height = 480;
            //avcodec_context->framerate = {1, 30};
        }
    }

    bool prepare(format_context_ptr& avformat_context,
                 AVRational input_framerate,
                 DecoderCodecContextDefinition const& decoder)
    {
        avstream = format_new_stream(avformat_context);

        index = decoder.index;
        avmedia_type = decoder.avmedia_type;

        if (options.transcode.empty())
        {
            avcodec_parameters_copy(avstream->codecpar, decoder.avstream->codecpar);
            return true;
        }

        assert(options.transcode.type() == AdminModel::MediaTypeDescriptionAVStreamTranscode::rtt);
        AdminModel::MediaTypeDescriptionAVStreamTranscode const* transcode_options;
        options.transcode.get(transcode_options);

        if (false == create_avcodec(transcode_options->codec))
            return false;

        avcodec_context = codec_context_alloc(avcodec);
        if (nullptr == avcodec_context)
        {
            //logging("could not allocate memory for codec context");
            return false;
        }

        avcodec_context_init(*transcode_options, decoder, input_framerate);

        if (0 > avcodec_open2(avcodec_context.get(), avcodec.get(), nullptr))
        {
            //logging("could not open the codec");
            return false;
        }
        avcodec_parameters_from_context(avstream->codecpar, avcodec_context.get());

        {
            filter_graph = filter_graph_alloc();

            int count;
            if (avmedia_type == AVMEDIA_TYPE_VIDEO)
            {
                frame_rate = avcodec_context->framerate;
                width = avcodec_context->width;
                height = avcodec_context->height;
                if (avcodec_context->pix_fmt != AV_PIX_FMT_NONE)
                    format = avcodec_context->pix_fmt;
                else if (avcodec->pix_fmts)
                {
                    count = 0;
                    while (avcodec->pix_fmts[count] != AV_PIX_FMT_NONE)
                    {
                        formats.push_back(avcodec->pix_fmts[count]);
                        ++count;
                    }
                }
                if (avcodec_context->framerate.den != 0 &&
                    avcodec_context->framerate.num != 0)
                    frame_rate = avcodec_context->framerate;
                else if (avcodec->supported_framerates)
                {
                    count = 0;
                    while (avcodec->supported_framerates[count].den != 0 &&
                           avcodec->supported_framerates[count].num != 0)
                    {
                        frame_rates.push_back(avcodec->supported_framerates[count]);
                        ++count;
                    }
                }
                //
                //
                AVFilterContext* buffer_context = nullptr;
                AVFilterContext* scale_context = nullptr;
                AVFilterContext* framerate_context = nullptr;

                {
                    string buffer_name = "buffer_" + std::to_string(index);
                    string buffer_arguments;
                    buffer_arguments += "video_size=" + std::to_string(width) + "x" + std::to_string(height);
                    buffer_arguments += ":pix_fmt=" + std::to_string(format);
                    buffer_arguments += ":time_base=" + std::to_string(avstream->time_base.num) +
                                        "/" + std::to_string(avstream->time_base.den);

                    auto sar = avstream->sample_aspect_ratio;
                    if(0 == sar.den)
                        sar = (AVRational){0,1};
                    buffer_arguments += ":pixel_aspect=" + std::to_string(sar.num) +
                                        "/" + std::to_string(sar.den);
                    if (frame_rate.num && frame_rate.den)
                    {
                        buffer_arguments += ":frame_rate=" + std::to_string(frame_rate.num) +
                                            "/" + std::to_string(frame_rate.den);
                    }

                    if (0 > avfilter_graph_create_filter(&buffer_context,
                                                         avfilter_get_by_name("buffer"),
                                                         buffer_name.c_str(),
                                                         buffer_arguments.c_str(),
                                                         nullptr,
                                                         filter_graph.get()))
                        return false;
                }

                if (width || height)
                {
                    string scale_name = "scale_" + std::to_string(index);
                    string scale_arguments = std::to_string(width) + ":" + std::to_string(height);

                    if (0 > avfilter_graph_create_filter(&scale_context,
                                                         avfilter_get_by_name("scale"),
                                                         scale_name.c_str(),
                                                         scale_arguments.c_str(),
                                                         nullptr,
                                                         filter_graph.get()))
                        return false;
                }

                if (0 != frame_rate.num &&
                    0 != frame_rate.den)
                {
                    string framerate_arguments = "fps=" + std::to_string(frame_rate.num) +
                                                 "/" + std::to_string(frame_rate.den);
                    string framerate_name = "fps_" + std::to_string(index);

                    if (0 > avfilter_graph_create_filter(&framerate_context,
                                                         avfilter_get_by_name("fps"),
                                                         framerate_name.c_str(),
                                                         framerate_arguments.c_str(),
                                                         nullptr,
                                                         filter_graph.get()))
                        return false;
                }

                {
                    string sink_name = "sink_" + std::to_string(index);

                    if (0 > avfilter_graph_create_filter(&filter_context_sink,
                                                         avfilter_get_by_name("buffersink"),
                                                         sink_name.c_str(),
                                                         nullptr,
                                                         nullptr,
                                                         filter_graph.get()))
                        return false;
                }

                AVFilterContext* current = nullptr;
                if (buffer_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, buffer_context, 0))
                            return false;

                    current = buffer_context;
                    if (nullptr == filter_context_source)
                        filter_context_source = current;
                }
                if (scale_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, scale_context, 0))
                            return false;

                    current = scale_context;
                    if (nullptr == filter_context_source)
                        filter_context_source = current;
                }
                if (framerate_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, framerate_context, 0))
                            return false;

                    current = framerate_context;
                    if (nullptr == filter_context_source)
                        filter_context_source = current;
                }
                //if (filter_context_sink)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, filter_context_sink, 0))
                            return false;

                    current = filter_context_sink;
                    if (nullptr == filter_context_source)
                        filter_context_source = current;
                }

                if (0 > avfilter_graph_config(filter_graph.get(), nullptr))
                    return false;
            }
            else if (avmedia_type == AVMEDIA_TYPE_AUDIO)
            {
                if (avcodec_context->sample_fmt != AV_SAMPLE_FMT_NONE)
                    format = avcodec_context->sample_fmt;
                else if (avcodec->sample_fmts)
                {
                    count = 0;
                    while (avcodec->sample_fmts[count] != AV_SAMPLE_FMT_NONE)
                    {
                        formats.push_back(avcodec->sample_fmts[count]);
                        ++count;
                    }
                }
                if (avcodec_context->sample_rate)
                    sample_rate = avcodec_context->sample_rate;
                else if (avcodec->supported_samplerates)
                {
                    count = 0;
                    while (avcodec->supported_samplerates[count])
                    {
                        sample_rates.push_back(avcodec->supported_samplerates[count]);
                        ++count;
                    }
                }
                if (avcodec_context->channels)
                    channel_layout = av_get_default_channel_layout(avcodec_context->channels);
                else if (avcodec->channel_layouts)
                {
                    count = 0;
                    while (avcodec->channel_layouts[count])
                    {
                        channel_layouts.push_back(avcodec->channel_layouts[count]);
                        ++count;
                    }
                }
                //
                //
                AVFilterContext* buffer_context = nullptr;
                AVFilterContext* format_context = nullptr;
                {
                    string buffer_name = "in_" + std::to_string(index);
                    string buffer_argument;
                    buffer_argument += "time_base=" + std::to_string(avstream->time_base.num) +
                                       "/" + std::to_string(avstream->time_base.den);
                    buffer_argument += ":sample_rate=" + std::to_string(avcodec_context->sample_rate);
                    buffer_argument += ":sample_fmt=";
                    buffer_argument += av_get_sample_fmt_name(avcodec_context->sample_fmt);
                    buffer_argument += ":channel_layout=";
                    std::stringstream sstream;
                    sstream << std::hex << avcodec_context->channel_layout;
                    buffer_argument += /*"0x" + */sstream.str();

                    if (0 > avfilter_graph_create_filter(&buffer_context,
                                                         avfilter_get_by_name("abuffer"),
                                                         buffer_name.c_str(),
                                                         buffer_argument.c_str(),
                                                         nullptr,
                                                         filter_graph.get()))
                        return false;
                }
                {
                    string format_name = "format_" + std::to_string(index);
                    string arg_sample_rate;
                    if (sample_rate)
                        arg_sample_rate = std::to_string(sample_rate);
                    else
                    {
                        for (size_t index = 0; index != sample_rates.size(); ++index)
                        {
                            arg_sample_rate += std::to_string(sample_rates[index]);
                            if (index != sample_rates.size() - 1)
                                arg_sample_rate += "|";
                        }
                    }
                    if (false == arg_sample_rate.empty())
                        arg_sample_rate = "sample_rates=" + arg_sample_rate;

                    string arg_sample_fmt;
                    if (format)
                        arg_sample_fmt = av_get_sample_fmt_name(AVSampleFormat(format));
                    else
                    {
                        for (size_t index = 0; index != formats.size(); ++index)
                        {
                            arg_sample_fmt += av_get_sample_fmt_name(AVSampleFormat(formats[index]));
                            if (index != formats.size() - 1)
                                arg_sample_fmt += "|";
                        }
                    }
                    if (false == arg_sample_fmt.empty())
                        arg_sample_fmt = "sample_fmts=" + arg_sample_fmt;

                    string arg_channel_layout;
                    if (channel_layout)
                    {
                        std::stringstream sstream;
                        sstream << std::hex << avcodec_context->channel_layout;
                        arg_channel_layout = /*"0x" + */sstream.str();
                    }
                    else
                    {
                        for (size_t index = 0; index != channel_layouts.size(); ++index)
                        {
                            std::stringstream sstream;
                            sstream << std::hex << avcodec_context->channel_layout;
                            arg_channel_layout += /*"0x" + */sstream.str();
                            if (index != channel_layouts.size() - 1)
                                arg_channel_layout += "|";
                        }
                    }
                    if (false == arg_channel_layout.empty())
                        arg_channel_layout = "channel_layouts=" + arg_channel_layout;

                    string format_argument;
                    if (false == arg_sample_rate.empty())
                        format_argument += arg_sample_rate + ":";
                    if (false == arg_sample_fmt.empty())
                        format_argument += arg_sample_fmt + ":";
                    if (false == arg_channel_layout.empty())
                        format_argument += arg_channel_layout + ":";

                    if (0 > avfilter_graph_create_filter(&format_context,
                                                         avfilter_get_by_name("aformat"),
                                                         format_name.c_str(),
                                                         format_argument.c_str(),
                                                         nullptr,
                                                         filter_graph.get()))
                        return false;
                }

                {
                    string sink_name = "sink_" + std::to_string(index);

                    if (0 > avfilter_graph_create_filter(&filter_context_sink,
                                                         avfilter_get_by_name("abuffersink"),
                                                         sink_name.c_str(),
                                                         nullptr,
                                                         nullptr,
                                                         filter_graph.get()))
                        return false;
                }
                AVFilterContext* current = nullptr;
                if (buffer_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, buffer_context, 0))
                            return false;

                    current = buffer_context;
                    if (nullptr == filter_context_source)
                        filter_context_source = current;
                }
                if (format_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, format_context, 0))
                            return false;

                    current = format_context;
                    if (nullptr == filter_context_source)
                        filter_context_source = current;
                }
                //if (filter_context_sink)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, filter_context_sink, 0))
                            return false;

                    current = filter_context_sink;
                    if (nullptr == filter_context_source)
                        filter_context_source = current;
                }

                if (0 > avfilter_graph_config(filter_graph.get(), nullptr))
                    return false;

            }
        }

        return true;
    }
};

template <typename TCodecContextDefinition>
class Context
{
public:
    format_context_ptr avformat_context;
    vector<TCodecContextDefinition> definitions;

    Context()
        : avformat_context(format_context_null())
    {}

    bool codec_context_definition_by_stream(int index,
                                            TCodecContextDefinition* &pdefinition)
    {
        for (auto& def : definitions)
        {
            if (def.index == index)
            {
                pdefinition = &def;
                return true;
            }
        }

        return false;
    }
};

class Input
{
public:
    Input() = default;
    Input(Input const&) = delete;
    Input(Input&&) = default;

    bool more = false;
    packet_ptr packet = packet_null();
    vector<frame_ptr> frames;
    int stream_index;

    ~Input()
    {
        for (auto& frame : frames)
            frame_unref(frame);

        if (packet)
            packet_unref(packet);
    }
};

class DecoderContext;
class EncoderContext : public Context<EncoderCodecContextDefinition>
{
public:
    string filepath;
    string type_definition_str;
    AVDictionary* muxer_opts = nullptr;

    AVFilterGraph *graph;

    bool load(packet&& options,
              DecoderContext& decoder);
    bool process(DecoderContext& decoder_context,
                 Input& input);
    bool final(DecoderContext& decoder_context);
};

class DecoderContext : public Context<DecoderCodecContextDefinition>
{
public:
    //string filepath;

    bool load(string const& path);
    std::pair<bool, Input> next(vector<EncoderContext>& encoder_contexts);
protected:
    bool scan_avformat_context()
    {
        for (int index = 0; index < int(avformat_context->nb_streams); ++index)
        {
            AVStream& avstream = *avformat_context->streams[index];
            if (false == add_definition(avstream, index))
                return false;
        }

        return true;
    }
    bool add_definition(AVStream& avstream,
                        int index)
    {
        if (avstream.codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
            avstream.codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            definitions.push_back(DecoderCodecContextDefinition());
            auto& decoder = definitions.back();
            return decoder.fill_stream_info(avstream, index);
        }

        //logging("skipping streams other than audio and video");
        return true;
    }
};

bool DecoderContext::load(string const& path)
{
    avformat_context = format_context_alloc_input(path);
    if (nullptr == avformat_context)
    {
        //logging("failed to init format context");
        return false;
    }

    if (false == scan_avformat_context())
        return false;

    //filepath = path;

    return true;
}

std::pair<bool, Input> DecoderContext::next(vector<EncoderContext>& encoder_contexts)
{
    std::pair<bool, Input> result;
    result.first = false;

    packet_ptr input_packet = packet_alloc();
    if (nullptr == input_packet)
    {
        //logging("failed to allocate memory for AVPacket");
        return result;
    }

    while (av_read_frame(avformat_context.get(), input_packet.get()) >= 0)
    {
        result.second.stream_index = input_packet->stream_index;

        bool input_frames_done = false;
        bool input_packet_done = false;

        for (auto& encoder_context : encoder_contexts)
        {
            DecoderCodecContextDefinition* pdecoder = nullptr;
            EncoderCodecContextDefinition* pencoder = nullptr;

            if (codec_context_definition_by_stream(result.second.stream_index, pdecoder) &&
                encoder_context.codec_context_definition_by_stream(result.second.stream_index, pencoder) &&
                pencoder && pdecoder)
            {
                EncoderCodecContextDefinition& encoder = *pencoder;
                DecoderCodecContextDefinition& decoder = *pdecoder;

                if (encoder.options.transcode.empty() &&
                    false == input_packet_done)
                {
                    input_packet_done = true;
                }
                else if (false == encoder.options.transcode.empty() &&
                         false == input_frames_done)
                {
                    input_frames_done = true;

                    int response = avcodec_send_packet(decoder.avcodec_context.get(),
                                                       input_packet.get());
                    if (response < 0)
                    {
                        //logging("Error while sending packet to decoder: %s", av_err2str(response));
                        return result;
                    }

                    frame_ptr input_frame = frame_alloc();

                    while (response >= 0)
                    {
                        if (nullptr == input_frame)
                            input_frame = frame_alloc();

                        if (nullptr == input_frame)
                        {
                            //logging("failed to allocate memory for AVFrame");
                            return result;
                        }
                        else
                        {
                            response = avcodec_receive_frame(decoder.avcodec_context.get(),
                                                             input_frame.get());
                            if (response == AVERROR(EAGAIN) ||
                                response == AVERROR_EOF)
                                break;
                            else if (response < 0)
                            {
                                //logging("Error while receiving frame from decoder: %s", av_err2str(response));
                                return result;
                            }
                            else
                            {
                                result.second.frames.push_back(std::move(input_frame));
                            }
                        }
                    }
                }
            }
        }

        if (input_packet_done)
            result.second.packet = std::move(input_packet);

        if (result.second.frames.size() ||
            result.second.packet)
        {
            result.first = true;
            result.second.more = true;
            return result;
        }
    }

    result.first = true;
    return result;
}

bool EncoderContext::load(packet&& options,
                          DecoderContext& decoder_context)
{
    if (options.type() != AdminModel::MediaTypeDescriptionVideoContainer::rtt)
        return true;
    AdminModel::MediaTypeDescriptionVideoContainer container_options;
    std::move(options).get(container_options);

    for (auto const& decoder : decoder_context.definitions)
    {
        EncoderCodecContextDefinition encoder;

        //  input_framerate is relevant only for (decoder.avmedia_type == AVMEDIA_TYPE_VIDEO)
        AVRational input_framerate = av_guess_frame_rate(decoder_context.avformat_context.get(),
                                                         decoder.avstream.get(),
                                                         nullptr);

        packet* option = nullptr;
        if (decoder.avmedia_type == AVMEDIA_TYPE_VIDEO)
            option = &container_options.video;
        else if (decoder.avmedia_type == AVMEDIA_TYPE_AUDIO)
            option = &container_options.audio;

        if (option && option->type() == AdminModel::MediaTypeDescriptionAVStream::rtt)
        {
            option->get(encoder.options);
            if (false == encoder.prepare(avformat_context,
                                         input_framerate,
                                         decoder))
                return false;

            definitions.push_back(std::move(encoder));
        }
    }

    if (avformat_context->oformat->flags & AVFMT_GLOBALHEADER)
        avformat_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (!(avformat_context->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open(&avformat_context->pb, filepath.c_str(), AVIO_FLAG_WRITE) < 0)
        {
            //logging("could not open the output file");
            return false;
        }
    }

    if (container_options.mux.type() == AdminModel::MediaTypeDescriptionAVStreamMux::rtt)
    {
        AdminModel::MediaTypeDescriptionAVStreamMux muxer_options;
        std::move(container_options).mux.get(muxer_options);
        av_dict_set(&muxer_opts,
                    muxer_options.muxer_opt_key.c_str(),
                    muxer_options.muxer_opt_value.c_str(),
                    0);
    }

    if (0 > avformat_write_header(avformat_context.get(),
                                  &muxer_opts))
    {
        //logging("an error occurred when opening output file");
        return false;
    }

    return true;
}
bool EncoderContext::process(DecoderContext& decoder_context,
                             Input& input)
{
    frame_ptr filter_frame = frame_alloc();
    if (nullptr == filter_frame)
    {
        //logging("failed to allocate memory for AVFrame");
        return false;
    }

    bool flush = false;
    if (input.frames.empty() && nullptr == input.packet)
        flush = true;

    for (auto& encoder : definitions)
    {
        if (false == flush &&
            input.stream_index != encoder.index)
            continue;

        DecoderCodecContextDefinition* pdecoder = nullptr;
        if (false == decoder_context.codec_context_definition_by_stream(encoder.index, pdecoder) ||
            nullptr == pdecoder)
            return false;

        DecoderCodecContextDefinition& decoder = *pdecoder;

        if (encoder.options.transcode.empty())
        {
            if (input.packet)
            {
                packet_ptr output_packet = packet_alloc();
                if (nullptr == output_packet)
                {
                    //logging("failed to allocate memory for AVPacket");
                    return false;
                }
                av_init_packet(output_packet.get());
                av_packet_ref(output_packet.get(), input.packet.get());

                av_packet_rescale_ts(output_packet.get(),
                                     decoder.avstream->time_base,
                                     encoder.avstream->time_base);
                int response;
                response = av_interleaved_write_frame(avformat_context.get(),
                                                      output_packet.get());
                if (response != 0)
                {
                    //logging("error while copying stream packet");
                    return false;
                }
            }
        }
        else
        {
            auto* pinput_frames = &input.frames;
            vector<frame_ptr> for_flush;
            for_flush.push_back(beltpp::t_unique_nullptr<AVFrame>());
            if (flush)
                pinput_frames = &for_flush;

            for (auto& input_frame : *pinput_frames)
            {
                if (input_frame && encoder.avmedia_type == AVMEDIA_TYPE_VIDEO)
                    input_frame->pict_type = AV_PICTURE_TYPE_NONE;

                //  avfilter processing block
                if (encoder.filter_context_sink &&
                    encoder.filter_context_source)
                {
                    //  video example shows AV_BUFFERSRC_FLAG_KEEP_REF instead of 0 below
                    if (0 > av_buffersrc_add_frame_flags(encoder.filter_context_source,
                                                         input_frame.get(),
                                                         0))
                    {
                        //av_log(nullptr, AV_LOG_ERROR, "Error while feeding the audio filtergraph\n");
                        return false;
                    }
                    /* pull filtered audio from the filtergraph */
                    int response;
                    while (true)
                    {
                        response = av_buffersink_get_frame(encoder.filter_context_sink,
                                                           filter_frame.get());
                        if (response == AVERROR(EAGAIN) ||
                            response == AVERROR_EOF)
                            break;
                        else if (response < 0)
                            return false;

                        frame_unref(input_frame);
                        input_frame = std::move(filter_frame);
                    }
                }
                //  avfilter processing done

                packet_ptr output_packet = packet_alloc();
                if (nullptr == output_packet)
                {
                    //logging("could not allocate memory for output packet");
                    return false;
                }

                //  encode the frame
                int response = avcodec_send_frame(encoder.avcodec_context.get(),
                                                  input_frame.get());

                while (response >= 0)
                {
                    response = avcodec_receive_packet(encoder.avcodec_context.get(),
                                                      output_packet.get());
                    if (response == AVERROR(EAGAIN) ||
                        response == AVERROR_EOF)
                        break;
                    else if (response < 0)
                    {
                        //logging("Error while receiving packet from encoder: %s", av_err2str(response));
                        return false;
                    }

                    output_packet->stream_index = decoder.index;
                    if (encoder.avmedia_type == AVMEDIA_TYPE_VIDEO)
                        output_packet->duration = encoder.avstream->time_base.den /
                                                  encoder.avstream->time_base.num /
                                                  decoder.avstream->avg_frame_rate.num *
                                                  decoder.avstream->avg_frame_rate.den;

                    av_packet_rescale_ts(output_packet.get(),
                                         decoder.avstream->time_base,
                                         encoder.avstream->time_base);
                    response = av_interleaved_write_frame(avformat_context.get(),
                                                          output_packet.get());
                    if (response != 0)
                    {
                        //logging("Error %d while receiving packet from decoder: %s", response, av_err2str(response));
                        return false;
                    }
                }
                packet_unref(output_packet);
            }
        }

        if (flush)
            encoder.avcodec_context.reset();
    }

    if (flush)
    {
        av_write_trailer(avformat_context.get());

        if (muxer_opts != nullptr)
        {
            av_dict_free(&muxer_opts);
            muxer_opts = nullptr;
        }

        avformat_context.reset();
    }

    return true;
}

class transcoder_detail
{
public:
    DecoderContext decoder;
    vector<EncoderContext> encoders;
};

transcoder::transcoder()
    : pimpl(new transcoder_detail())
{}
transcoder::~transcoder() = default;

bool transcoder::init(vector<packet>&& options)
{
    if (false == pimpl->decoder.load(input_file.string()))
        return false;

    size_t option_index = 0;
    for (auto&& option : options)
    {
        ++option_index;

        EncoderContext encoder_context;
        encoder_context.filepath = (output_dir / (std::to_string(pimpl->encoders.size()) + ".mp4")).string();
        encoder_context.type_definition_str = option.to_string();
        encoder_context.avformat_context = format_context_alloc_output(encoder_context.filepath);
        if (nullptr == encoder_context.avformat_context)
        {
            //logging("could not allocate memory for output format");
            return false;
        }

        if (false == encoder_context.load(std::move(option), pimpl->decoder))
            return false;

        if (false == encoder_context.definitions.empty())
            pimpl->encoders.push_back(std::move(encoder_context));
    }

    state = before_loop;

    return true;
}

bool transcoder::loop(unordered_map<string, string>& filename_to_type_definition)
{
    filename_to_type_definition.clear();
    while (true)
    {   //  for now this will not actually do chunk by chunk encoding
        auto input = pimpl->decoder.next(pimpl->encoders);
        if (false == input.first)
            return false;

        for (auto& encoder_context : pimpl->encoders)
        {
            if (false == encoder_context.process(pimpl->decoder,
                                                 input.second))
                return false;

            filename_to_type_definition[encoder_context.filepath] = encoder_context.type_definition_str;
        }

        if (false == input.second.more)
            break;
    }

    return true;
}

bool transcoder::clean()
{
    pimpl->decoder.avformat_context.reset();

    for (auto& decoder : pimpl->decoder.definitions)
        decoder.avcodec_context.reset();

    return true;
}

void transcoder::run(unordered_map<string, string>& filename_to_type_definition)
{
    if (before_loop == state &&
        loop(filename_to_type_definition))
    {
        state = done;
    }
    else if (done == state)
        clean();
}
}
