#include "libavwrapper.hpp"
#include "admin_model.hpp"
#include "internal_model.hpp"
#include "worker.hpp"

#include <mesh.pp/cryptoutility.hpp>

#include <boost/filesystem/path.hpp>

//#include <iostream>

#define __STDC_CONSTANT_MACROS

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>
#include <libavutil/opt.h>
#include <libavutil/display.h>
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
#include <cmath>

using std::string;
using beltpp::packet;
using std::vector;
using std::unordered_map;
using std::pair;
namespace filesystem = boost::filesystem;

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
    if (ptr)
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

struct rotation_angle
{
    size_t whole;
    size_t fraction;
    static size_t const fractions_in_whole = 100;
    double const pi = 3.14159265;

    bool operator == (size_t theta) const
    {
        return fraction == 0 && whole == theta;
    }
    bool operator != (size_t theta) const
    {
        return false == (operator == (theta));
    }

    string to_string() const
    {
        return std::to_string(whole) + "." + std::to_string(fraction);
    }

    double sin() const
    {
        return std::sin((whole + fraction / double(fractions_in_whole)) * pi / 180);
    }

    double cos() const
    {
        return std::cos((whole + fraction / double(fractions_in_whole)) * pi / 180);
    }
};

rotation_angle get_rotation(AVStream* st, double rotate)
{
    uint8_t* displaymatrix = av_stream_get_side_data(st,
                                                     AV_PKT_DATA_DISPLAYMATRIX,
                                                     nullptr);
    double theta = 0;
    if (displaymatrix)
        theta = -av_display_rotation_get(reinterpret_cast<int32_t*>(displaymatrix));

    //theta -= 360 * floor(theta / 360 + 0.9 / 360);
    theta += rotate;
    theta -= 360 * floor(theta / 360);

    rotation_angle angle;
    angle.whole = floor(theta);
    angle.fraction = floor((theta - angle.whole) * rotation_angle::fractions_in_whole);

    // if (fabs(theta - 90 * round(theta / 90)) > 2)
    //     av_log(NULL, AV_LOG_WARNING, "Odd rotation angle.\n"
    //         "If you want to help, upload a sample "
    //         "of this file to ftp://upload.ffmpeg.org/incoming/ "
    //         "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)");

    return angle;
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
};

class DataUnit
{
public:
    DataUnit() = default;
    DataUnit(DataUnit const&) = delete;
    DataUnit(DataUnit&&) = default;

    bool more_read_packet = false;
    bool more_read_frame = false;
    bool more_write_packet = false;
    bool more_write_frame = false;

    packet_ptr packet = packet_alloc();
    frame_ptr frame = frame_alloc();
    int stream_index;

    ~DataUnit()
    {
        frame_unref(frame);
        packet_unref(packet);
    }
};

class DecoderCodecContextDefinition : public CodecContextDefinition
{
public:
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

class EncoderCodecContextDefinition : public CodecContextDefinition
{
public:
    AdminModel::MediaTypeDescriptionAVStream* options = nullptr;

    size_t duration = 0;
    frame_ptr frame = frame_alloc();
    packet_ptr packet = packet_alloc();

    AVFilterContext* filter_context_source = nullptr;
    AVFilterContext* filter_context_sink = nullptr;
    filter_graph_ptr filter_graph = filter_graph_null();

    //vector<AVRational> frame_rates;
    //vector<int> formats;
    //vector<int> sample_rates;
    //vector<uint64_t> channel_layouts;

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

    void avcodec_context_init(AdminModel::MediaTypeDescriptionAVStreamTranscode& options,
                              DecoderCodecContextDefinition const& decoder,
                              AVRational input_framerate,
                              bool& skip)
    {
        skip = false;

        if (options.parameters)
        for (auto const& item : *options.parameters)
            av_opt_set(avcodec_context->priv_data, item.first.c_str(), item.second.c_str(), 0);

        if (decoder.avmedia_type == AVMEDIA_TYPE_AUDIO)
        {
            /*
            int count = 0;
            //  this code may be needed with some encoder
            if (avcodec->supported_samplerates)
            {
                count = 0;
                while (avcodec->supported_samplerates[count])
                {
                    sample_rates.push_back(avcodec->supported_samplerates[count]);
                    ++count;
                }
            }
            //  this one too
            if (avcodec->sample_fmts)
            {
                count = 0;
                while (avcodec->sample_fmts[count] != AV_SAMPLE_FMT_NONE)
                {
                    formats.push_back(avcodec->sample_fmts[count]);
                    ++count;
                }
            }
            //  also, maybe, this.
            if (avcodec->channel_layouts)
            {
                count = 0;
                while (avcodec->channel_layouts[count])
                {
                    channel_layouts.push_back(avcodec->channel_layouts[count]);
                    ++count;
                }
            }
            */
            //
            //int OUTPUT_CHANNELS = 2;
            int OUTPUT_BIT_RATE = 196000;
            avcodec_context->channels       = decoder.avcodec_context->channels;
            avcodec_context->channel_layout = decoder.avcodec_context->channel_layout;//av_get_default_channel_layout(avcodec_context->channels);
            avcodec_context->sample_rate    = decoder.avcodec_context->sample_rate;
            avcodec_context->sample_fmt     = avcodec->sample_fmts[0];
            avcodec_context->bit_rate       = OUTPUT_BIT_RATE;
            avcodec_context->time_base      = (AVRational){1, decoder.avcodec_context->sample_rate};

            avcodec_context->strict_std_compliance = FF_COMPLIANCE_NORMAL;

            //avstream->time_base = avcodec_context->time_base;
        }
        else if (decoder.avmedia_type == AVMEDIA_TYPE_VIDEO)
        {
            avcodec_context->sample_aspect_ratio = decoder.avcodec_context->sample_aspect_ratio;
            if (avcodec->pix_fmts)
                avcodec_context->pix_fmt = avcodec->pix_fmts[0];
            else
                avcodec_context->pix_fmt = decoder.avcodec_context->pix_fmt;

            //avcodec_context->bit_rate = 2 * 1000 * 1000;
            //avcodec_context->rc_buffer_size = 4 * 1000 * 1000;
            //avcodec_context->rc_max_rate = 2 * 1000 * 1000;
            //avcodec_context->rc_min_rate = 2.5 * 1000 * 1000;

            // let below options remain here, might be useful for troubleshooting
            //avcodec_context->profile = FF_PROFILE_H264_BASELINE;
            //avcodec_context->level = 31;
            //avcodec_context->bits_per_coded_sample = 24;
            //avcodec_context->bits_per_raw_sample = 8;
            //avcodec_context->chroma_sample_location = AVCHROMA_LOC_LEFT;

            //  this code may be needed with some encoder
            /*
            if (avcodec->supported_framerates)
            {
                count = 0;
                while (avcodec->supported_framerates[count].den != 0 &&
                       avcodec->supported_framerates[count].num != 0)
                {
                    frame_rates.push_back(avcodec->supported_framerates[count]);
                    ++count;
                }
            }
            //  this one too
            if (avcodec->pix_fmts)
            {
                count = 0;
                while (avcodec->pix_fmts[count] != AV_PIX_FMT_NONE)
                {
                    formats.push_back(avcodec->pix_fmts[count]);
                    ++count;
                }
            }
            */

            if (!options.filter ||
                (*options.filter)->type() != AdminModel::MediaTypeDescriptionVideoFilter::rtt)
            {
                avcodec_context->height = decoder.avcodec_context->height;
                avcodec_context->width = decoder.avcodec_context->width;
                avcodec_context->framerate = input_framerate;
            }
            else
            {
                AdminModel::MediaTypeDescriptionVideoFilter* filter;
                (*options.filter)->get(filter);

                double const decoder_input_height = decoder.avcodec_context->height;
                double const decoder_input_width = decoder.avcodec_context->width;

                rotation_angle angle = get_rotation(decoder.avstream.get(), filter->rotate);

                double const input_height = fabs(decoder_input_width * angle.sin()) + fabs(decoder_input_height * angle.cos());
                double const input_width = fabs(decoder_input_width * angle.cos()) + fabs(decoder_input_height * angle.sin());
                
                double height_ratio = double(filter->height) / input_height;
                double width_ratio = double(filter->width) / input_width;

                if (height_ratio >= width_ratio &&
                    width_ratio <= 1)
                {
                    avcodec_context->height = input_height * width_ratio;
                    avcodec_context->width = filter->width;
                }
                else if (height_ratio <= width_ratio &&
                         height_ratio <= 1)
                {
                    avcodec_context->height = filter->height;
                    avcodec_context->width = input_width * height_ratio;
                }
                else
                {
                    avcodec_context->height = input_height;
                    avcodec_context->width = input_width;
                }
                if (0 != avcodec_context->height % 2)
                    --avcodec_context->height;
                if (0 != avcodec_context->width % 2)
                    --avcodec_context->width;

                int ratio = input_framerate.num / input_framerate.den +
                                          (0 == input_framerate.num % input_framerate.den ? 0 : 1);

                if (int(filter->fps) > ratio)
                {
                    if (false == filter->adjust)
                    {
                        skip = true;
                        return;
                    }
                    filter->fps = ratio;
                }
                avcodec_context->framerate = {int(filter->fps), 1};

                if (false == filter->adjust &&
                    (
                        int(filter->height) != avcodec_context->height ||
                        int(filter->width) != avcodec_context->width
                    ))
                {
                    skip = true;
                    return;
                }

                filter->height = avcodec_context->height;
                filter->width = avcodec_context->width;
            }

            avcodec_context->time_base = av_inv_q(avcodec_context->framerate);

            avcodec_context->strict_std_compliance = FF_COMPLIANCE_NORMAL;
            //avstream->time_base = avcodec_context->time_base;
//            avcodec_context->time_base = av_mul_q(decoder.avstream->time_base,
//                                                  av_div_q(avcodec_context->framerate,
//                                                           input_framerate));
//            avstream->time_base = avcodec_context->time_base;
            //avstream->avg_frame_rate = avcodec_context->framerate;
            /*if (avcodec->supported_framerates)
            {
                int index = av_find_nearest_q_idx(avcodec_context->framerate, avcodec->supported_framerates);
                avcodec_context->framerate = avcodec->supported_framerates[index];
            }*/
        }
    }

    bool avfilter_context_init(AdminModel::MediaTypeDescriptionAVStreamTranscode const& options,
                               DecoderCodecContextDefinition const& decoder,
                               AVRational input_framerate)
    {

        {
            filter_graph = filter_graph_alloc();

            if (options.filter &&
                avmedia_type == AVMEDIA_TYPE_VIDEO)
            {
                AdminModel::MediaTypeDescriptionVideoFilter const* filter = nullptr;
                if (options.filter &&
                    (*options.filter)->type() == AdminModel::MediaTypeDescriptionVideoFilter::rtt)
                    (*options.filter)->get(filter);

                AVFilterContext* buffer_context = nullptr;
                AVFilterContext* rotate1_context = nullptr;
                AVFilterContext* rotate2_context = nullptr;
                AVFilterContext* deshake_context = nullptr;
                AVFilterContext* scale_context = nullptr;
                AVFilterContext* framerate_context = nullptr;

                {
                    string buffer_name = "buffer_" + std::to_string(index);
                    string buffer_arguments;
                    buffer_arguments += "video_size=" + std::to_string(decoder.avcodec_context->width) + "x" +
                                                        std::to_string(decoder.avcodec_context->height);
                    buffer_arguments += ":pix_fmt=" + std::to_string(decoder.avcodec_context->pix_fmt);
                    buffer_arguments += ":time_base=" + std::to_string(decoder.avstream->time_base.num) +
                                        "/" + std::to_string(decoder.avstream->time_base.den);

                    auto sar = decoder.avstream->sample_aspect_ratio;
                    if(0 == sar.den)
                        sar = (AVRational){0,1};
                    buffer_arguments += ":pixel_aspect=" + std::to_string(sar.num) +
                                        "/" + std::to_string(sar.den);
                    if (input_framerate.num && input_framerate.den)
                    {
                        buffer_arguments += ":frame_rate=" + std::to_string(input_framerate.num) +
                                            "/" + std::to_string(input_framerate.den);
                    }

                    if (0 > avfilter_graph_create_filter(&buffer_context,
                                                         avfilter_get_by_name("buffer"),
                                                         buffer_name.c_str(),
                                                         buffer_arguments.c_str(),
                                                         nullptr,
                                                         filter_graph.get()))
                        return false;
                }

                {
                    rotation_angle angle = get_rotation(decoder.avstream.get(), filter->rotate);

                    if (angle == 90)
                    {
                        string transpose_name = "transpose_" + std::to_string(index);
                        string transpose_arguments = "clock";

                        if (0 > avfilter_graph_create_filter(&rotate1_context,
                                                             avfilter_get_by_name("transpose"),
                                                             transpose_name.c_str(),
                                                             transpose_arguments.c_str(),
                                                             nullptr,
                                                             filter_graph.get()))
                            return false;
                    }
                    else if (angle == 180)
                    {
                        string hflip_name = "hflip_" + std::to_string(index);

                        if (0 > avfilter_graph_create_filter(&rotate1_context,
                                                             avfilter_get_by_name("hflip"),
                                                             hflip_name.c_str(),
                                                             nullptr,
                                                             nullptr,
                                                             filter_graph.get()))
                            return false;

                        string vflip_name = "vflip_" + std::to_string(index);

                        if (0 > avfilter_graph_create_filter(&rotate2_context,
                                                             avfilter_get_by_name("vflip"),
                                                             vflip_name.c_str(),
                                                             nullptr,
                                                             nullptr,
                                                             filter_graph.get()))
                            return false;
                    }
                    else if (angle == 270)
                    {
                        string transpose_name = "transpose_" + std::to_string(index);
                        string transpose_arguments = "cclock";

                        if (0 > avfilter_graph_create_filter(&rotate1_context,
                                                             avfilter_get_by_name("transpose"),
                                                             transpose_name.c_str(),
                                                             transpose_arguments.c_str(),
                                                             nullptr,
                                                             filter_graph.get()))
                            return false;
                    }
                    else
                    {
                        // fillcolor details
                        // https://ffmpeg.org/ffmpeg-utils.html#Color
                        // rotate details
                        // http://ffmpeg.org/ffmpeg-filters.html#rotate

                        string fillcolor = "none";
                        if (filter->background_color)
                            fillcolor = *filter->background_color;
                        string radians = angle.to_string() + "*PI/180";
                        string rotate_name = "rotate_" + std::to_string(index);
                        string rotate_arguments = "angle=" + radians + " : "
                                                  "fillcolor=" + fillcolor + " : " // none, 0x000000, black or white ...
                                                  "ow=rotw(" + radians + ") : "
                                                  "oh=roth(" + radians + ")";

                        if (0 > avfilter_graph_create_filter(&rotate1_context,
                                                             avfilter_get_by_name("rotate"),
                                                             rotate_name.c_str(),
                                                             rotate_arguments.c_str(),
                                                             nullptr,
                                                             filter_graph.get()))
                            return false;
                    }
                }

                if (filter && filter->stabilize && *filter->stabilize)
                {
                    string deshake_name = "deshake_" + std::to_string(index);
                    string deshake_arguments;

                    if (0 > avfilter_graph_create_filter(&deshake_context,
                                                         avfilter_get_by_name("deshake"),
                                                         deshake_name.c_str(),
                                                         deshake_arguments.c_str(),
                                                         nullptr,
                                                         filter_graph.get()))
                        return false;
                }

                if (filter)
                {
                    string scale_name = "scale_" + std::to_string(index);
                    string scale_arguments = std::to_string(avcodec_context->width) + ":" +
                                             std::to_string(avcodec_context->height);
                    scale_arguments += ":flags=bicubic";

                    if (0 > avfilter_graph_create_filter(&scale_context,
                                                         avfilter_get_by_name("scale"),
                                                         scale_name.c_str(),
                                                         scale_arguments.c_str(),
                                                         nullptr,
                                                         filter_graph.get()))
                        return false;
                }

                if (filter)
                {
                    string framerate_arguments = "fps=" + std::to_string(avcodec_context->framerate.num) +
                                                 "/" + std::to_string(avcodec_context->framerate.den);
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
                //if (buffer_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, buffer_context, 0))
                            return false;

                    current = buffer_context;
                    if (nullptr == filter_context_source)
                        filter_context_source = current;
                }
                if (rotate1_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, rotate1_context, 0))
                            return false;

                    current = rotate1_context;
                    // if (nullptr == filter_context_source)
                    //     filter_context_source = current;
                }
                if (rotate2_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, rotate2_context, 0))
                            return false;

                    current = rotate2_context;
                    // if (nullptr == filter_context_source)
                    //     filter_context_source = current;
                }
                if (deshake_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, deshake_context, 0))
                            return false;

                    current = deshake_context;
                    // if (nullptr == filter_context_source)
                    //     filter_context_source = current;
                }
                if (scale_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, scale_context, 0))
                            return false;

                    current = scale_context;
                    // if (nullptr == filter_context_source)
                    //     filter_context_source = current;
                }
                if (framerate_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, framerate_context, 0))
                            return false;

                    current = framerate_context;
                    // if (nullptr == filter_context_source)
                    //     filter_context_source = current;
                }
                //if (filter_context_sink)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, filter_context_sink, 0))
                            return false;

                    current = filter_context_sink;
                    // if (nullptr == filter_context_source)
                    //     filter_context_source = current;
                }

                if (0 > avfilter_graph_config(filter_graph.get(), nullptr))
                    return false;
            }
            else if (avmedia_type == AVMEDIA_TYPE_AUDIO)
            {
                AdminModel::MediaTypeDescriptionAudioFilter const* filter = nullptr;
                if (options.filter &&
                    (*options.filter)->type() == AdminModel::MediaTypeDescriptionAudioFilter::rtt)
                    (*options.filter)->get(filter);

                AVFilterContext* buffer_context = nullptr;
                AVFilterContext* format_context = nullptr;
                AVFilterContext* volume_context = nullptr;

                {
                    string buffer_name = "in_" + std::to_string(index);
                    string buffer_argument;
                    buffer_argument += "time_base=" + std::to_string(decoder.avstream->time_base.num) +
                                       "/" + std::to_string(decoder.avstream->time_base.den);
                    buffer_argument += ":sample_rate=" + std::to_string(decoder.avcodec_context->sample_rate);
                    buffer_argument += ":sample_fmt=";
                    buffer_argument += av_get_sample_fmt_name(decoder.avcodec_context->sample_fmt);
                    buffer_argument += ":channel_layout=";
                    std::stringstream sstream;

                    if (decoder.avcodec_context->channel_layout)
                        sstream << std::hex << decoder.avcodec_context->channel_layout;
                    else
                        sstream << std::hex << av_get_default_channel_layout(decoder.avcodec_context->channels);

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
                    // this code may be needed with some encoder
                    /*
                    for (size_t index = 0; index != sample_rates.size(); ++index)
                    {
                        arg_sample_rate += std::to_string(sample_rates[index]);
                        if (index != sample_rates.size() - 1)
                            arg_sample_rate += "|";
                    }
                    */
                    if (false == arg_sample_rate.empty())
                        arg_sample_rate = "sample_rates=" + arg_sample_rate;

                    string arg_sample_fmt;
                    //  this one too
                    /*
                    for (size_t index = 0; index != formats.size(); ++index)
                    {
                        arg_sample_fmt += av_get_sample_fmt_name(AVSampleFormat(formats[index]));
                        if (index != formats.size() - 1)
                            arg_sample_fmt += "|";
                    }
                    */
                    if (false == arg_sample_fmt.empty())
                        arg_sample_fmt = "sample_fmts=" + arg_sample_fmt;

                    string arg_channel_layout;
                    if (avcodec_context->channel_layout)
                    {
                        std::stringstream sstream;
                        sstream << std::hex << avcodec_context->channel_layout;
                        arg_channel_layout = /*"0x" + */sstream.str();
                    }/*
                    else
                    {
                        for (size_t index = 0; index != channel_layouts.size(); ++index)
                        {
                            std::stringstream sstream;
                            sstream << std::hex << avcodec_context->channel_layout;
                            arg_channel_layout += /-*"0x" + *-/sstream.str();
                            if (index != channel_layouts.size() - 1)
                                arg_channel_layout += "|";
                        }
                    }*/
                    if (false == arg_channel_layout.empty())
                        arg_channel_layout = "channel_layouts=" + arg_channel_layout;

                    string format_argument;
                    if (false == arg_sample_fmt.empty())
                        format_argument += arg_sample_fmt + ":";
                    if (false == arg_sample_rate.empty())
                        format_argument += arg_sample_rate + ":";
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

                if (filter)
                {
                    string volume_arguments = "volume=" + std::to_string(filter->volume);
                    string volume_name = "volume_" + std::to_string(index);

                    if (0 > avfilter_graph_create_filter(&volume_context,
                                                         avfilter_get_by_name("volume"),
                                                         volume_name.c_str(),
                                                         volume_arguments.c_str(),
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
                //if (buffer_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, buffer_context, 0))
                            return false;

                    current = buffer_context;
                    if (nullptr == filter_context_source)
                        filter_context_source = current;
                }
                //if (format_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, format_context, 0))
                            return false;

                    current = format_context;
                    // if (nullptr == filter_context_source)
                    //     filter_context_source = current;
                }
                if (volume_context)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, volume_context, 0))
                            return false;

                    current = volume_context;
                    // if (nullptr == filter_context_source)
                    //     filter_context_source = current;
                }
                //if (filter_context_sink)
                {
                    if (current &&
                        0 > avfilter_link(current, 0, filter_context_sink, 0))
                            return false;

                    current = filter_context_sink;
                    // if (nullptr == filter_context_source)
                    //     filter_context_source = current;
                }

                if (!(avcodec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
                    av_buffersink_set_frame_size(filter_context_sink, avcodec_context->frame_size);

                if (0 > avfilter_graph_config(filter_graph.get(), nullptr))
                    return false;
            }
        }

        return true;
    }

    bool prepare(format_context_ptr& avformat_context,
                 AVRational input_framerate,
                 DecoderCodecContextDefinition const& decoder,
                 bool& skip)
    {
        skip = false;

        avstream = format_new_stream(avformat_context);

        index = decoder.index;
        avmedia_type = decoder.avmedia_type;

        if (!options->transcode)
        {
            if (0 > avcodec_parameters_copy(avstream->codecpar, decoder.avstream->codecpar))
            {
                //logging("failed to copy codec parameters");
                return false;
            }
            return true;
        }

        if (false == create_avcodec(options->transcode->codec))
            return false;

        avcodec_context = codec_context_alloc(avcodec);
        if (nullptr == avcodec_context)
        {
            //logging("could not allocate memory for codec context");
            return false;
        }

        avcodec_context_init(*options->transcode, decoder, input_framerate, skip);

        if (skip)
            return true;

        // let below options remain here, might be useful for troubleshooting
        //AVDictionary* ret = nullptr;
        //av_dict_set(&ret, "level", "21", 0);
        //av_dict_set(&ret, "refs", "2", 0);
        //av_dict_set(&ret, "bt", "345k", 0);
        //av_dict_set(&ret, "threads", "0", 0);

        //  this is the flag that enables the encoder codex extradata
        //  so that ios and macos safari can play the h264 video
        //  not sure - need to check with below "if"?
        if (avformat_context->oformat->flags & AVFMT_GLOBALHEADER)
            avcodec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (0 > avcodec_open2(avcodec_context.get(), avcodec.get(), nullptr/*&ret*/))
        {
            //logging("could not open the codec");
            return false;
        }

        if (0 > avcodec_parameters_from_context(avstream->codecpar, avcodec_context.get()))
        {
            //logging("failed to fill avstream codec parameters");
            return false;
        }

        if (false == avfilter_context_init(*options->transcode, decoder, input_framerate))
            return false;

        return true;
    }

    bool process_encode_frame(format_context_ptr& avformat_context,
                              DecoderCodecContextDefinition const& decoder)
    {
        packet_unref(packet);

        //  encode the frame
        if (frame && avmedia_type == AVMEDIA_TYPE_VIDEO)
            frame->pict_type = AV_PICTURE_TYPE_NONE;
        int response = avcodec_send_frame(avcodec_context.get(),
                                          frame.get());

        while (response >= 0)
        {
            response = avcodec_receive_packet(avcodec_context.get(),
                                              packet.get());
            if (response == AVERROR(EAGAIN) ||
                response == AVERROR_EOF)
                break;
            else if (response < 0)
            {
                //logging("Error while receiving packet from encoder: %s", av_err2str(response));
                return false;
            }

            packet->stream_index = avstream->index;

            if (filter_context_sink)
            {
                AVRational filter_tb = av_buffersink_get_time_base(filter_context_sink);
                av_packet_rescale_ts(packet.get(),
                                     filter_tb,
                                     avstream->time_base);
            }
            else
                av_packet_rescale_ts(packet.get(),
                                     decoder.avstream->time_base,
                                     avstream->time_base);

            if (avmedia_type == AVMEDIA_TYPE_VIDEO)
                packet->duration = avstream->time_base.den /
                                   avstream->time_base.num *
                                   avcodec_context->framerate.den /
                                   avcodec_context->framerate.num;


            int64_t duration_local = 1000 * double(packet->dts) /
                                     double(avstream->time_base.den) * double(avstream->time_base.num);
            
            if (duration_local > 0)
                duration = duration_local;                       
            
            //std::cout << duration << ((avmedia_type == AVMEDIA_TYPE_VIDEO) ? "\tvideo\n" : "\taudio\n");

            if (0 != av_interleaved_write_frame(avformat_context.get(),
                                                packet.get()))
            {
                //logging("Error %d while receiving packet from decoder: %s", response, av_err2str(response));
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

class DecoderContext;
class EncoderContext : public Context<EncoderCodecContextDefinition>
{
public:
    size_t option_index = 0;
    string filepath;
    AVDictionary* muxer_opts = nullptr;

    AVFilterGraph *graph;

    bool load(size_t option_index,
              AdminModel::MediaTypeDescriptionVariant& options,
              DecoderContext& decoder,
              filesystem::path const& output_dir);
    bool process(DecoderContext& decoder_context,
                 DataUnit& data_unit);
    bool final(DecoderContext& decoder_context);
};

class DecoderContext : public Context<DecoderCodecContextDefinition>
{
public:
    bool load(string const& path);
    bool next(vector<EncoderContext>& encoder_contexts,
              DataUnit& data_unit);
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

bool DecoderContext::next(vector<EncoderContext>& encoder_contexts,
                          DataUnit& data_unit)
{
    if (data_unit.more_read_packet &&
        false == data_unit.more_write_packet &&
        false == data_unit.more_read_frame &&
        false == data_unit.more_write_frame)
    {
        packet_unref(data_unit.packet);

        if (0 > av_read_frame(avformat_context.get(), data_unit.packet.get()))
            data_unit.more_read_packet = false;
        else
        {
            data_unit.stream_index = data_unit.packet->stream_index;

            bool input_frames_done = false;
            bool input_packet_done = false;

            for (auto& encoder_context : encoder_contexts)
            {
                DecoderCodecContextDefinition* pdecoder = nullptr;
                EncoderCodecContextDefinition* pencoder = nullptr;

                if (codec_context_definition_by_stream(data_unit.stream_index, pdecoder) &&
                    encoder_context.codec_context_definition_by_stream(data_unit.stream_index, pencoder) &&
                    pencoder && pdecoder)
                {
                    EncoderCodecContextDefinition& encoder = *pencoder;
                    DecoderCodecContextDefinition& decoder = *pdecoder;

                    if (!encoder.options->transcode &&
                        false == input_packet_done)
                    {
                        input_packet_done = true;
                    }
                    else if (encoder.options->transcode &&
                             false == input_frames_done)
                    {
                        input_frames_done = true;

                        int response = avcodec_send_packet(decoder.avcodec_context.get(),
                                                           data_unit.packet.get());
                        if (response < 0)
                        {
                            //logging("Error while sending packet to decoder: %s", av_err2str(response));
                            return false;
                        }
                    }
                }
            }

            if (input_packet_done)
                data_unit.more_write_packet = true;
            if (input_frames_done)
                data_unit.more_read_frame = true;
        }
    }

    if (data_unit.more_read_frame &&
        false == data_unit.more_write_frame)
    {
        for (auto& encoder_context : encoder_contexts)
        {
            DecoderCodecContextDefinition* pdecoder = nullptr;
            EncoderCodecContextDefinition* pencoder = nullptr;

            if (codec_context_definition_by_stream(data_unit.stream_index, pdecoder) &&
                encoder_context.codec_context_definition_by_stream(data_unit.stream_index, pencoder) &&
                pencoder && pdecoder)
            {
                EncoderCodecContextDefinition& encoder = *pencoder;
                DecoderCodecContextDefinition& decoder = *pdecoder;

                if (encoder.options->transcode)
                {
                    int response;

                    frame_unref(data_unit.frame);

                    response = avcodec_receive_frame(decoder.avcodec_context.get(),
                                                     data_unit.frame.get());
                    if (response == AVERROR(EAGAIN) ||
                        response == AVERROR_EOF)
                        data_unit.more_read_frame = false;
                    else if (response < 0)
                    {
                        //logging("Error while receiving frame from decoder: %s", av_err2str(response));
                        return false;
                    }
                    else
                    {
                        data_unit.more_write_frame = true;
                    }
                }
                break;
            }
        }
    }

    return true;
}

bool EncoderContext::load(size_t option_index_,
                          AdminModel::MediaTypeDescriptionVariant& options,
                          DecoderContext& decoder_context,
                          filesystem::path const& output_dir)
{
    if (options->type() != AdminModel::MediaTypeDescriptionAVContainer::rtt)
        return true;

    option_index = option_index_;

    AdminModel::MediaTypeDescriptionAVContainer* container_options;
    options->get(container_options);

    filepath = (output_dir / (std::to_string(option_index_) + "." + container_options->container_extension)).string();

    avformat_context = format_context_alloc_output(filepath);
    if (nullptr == avformat_context)
    {
        //logging("could not allocate memory for output format");
        return false;
    }

    //av_dict_set(&avformat_context->metadata, "creation_time", nullptr, 0);

    for (auto const& decoder : decoder_context.definitions)
    {
        EncoderCodecContextDefinition encoder;

        //  input_framerate is relevant only for (decoder.avmedia_type == AVMEDIA_TYPE_VIDEO)
        AVRational input_framerate = av_guess_frame_rate(decoder_context.avformat_context.get(),
                                                         decoder.avstream.get(),
                                                         nullptr);

        bool is_set = false;
        if (decoder.avmedia_type == AVMEDIA_TYPE_VIDEO &&
            container_options->video)
        {
            encoder.options = &(*container_options->video);
            is_set = true;
        }
        else if (decoder.avmedia_type == AVMEDIA_TYPE_AUDIO &&
                 container_options->audio)
        {
            encoder.options = &(*container_options->audio);
            is_set = true;
        }
        
        if (is_set)
        {
            bool skip = false;
            if (false == encoder.prepare(avformat_context,
                                         input_framerate,
                                         decoder,
                                         skip))
                return false;

            if (skip)
            {
                definitions.clear();
                break;
            }
        
            definitions.push_back(std::move(encoder));
        }
    }

    if (definitions.empty())
        return true;

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

    if (container_options->muxer_parameters)
    for (auto const& item : *container_options->muxer_parameters)
    {
        if (item.first.empty() || item.second.empty())
            continue;

        av_dict_set(&muxer_opts,
                    item.first.c_str(),
                    item.second.c_str(),
                    0);
    }

    if (0 > avformat_write_header(avformat_context.get(),
                                  &muxer_opts))
    {
        //logging("an error occurred when opening output file");
        return false;
    }

    av_dump_format(avformat_context.get(), 0, filepath.c_str(), 1);

    return true;
}
bool EncoderContext::process(DecoderContext& decoder_context,
                             DataUnit& data_unit)
{
    bool flush = false;
    if (data_unit.more_read_packet == false)
        flush = true;

    if (data_unit.more_write_packet)
    {
        for (auto& encoder : definitions)
        {
            if (data_unit.stream_index != encoder.index)
                continue;

            DecoderCodecContextDefinition* pdecoder = nullptr;
            if (false == decoder_context.codec_context_definition_by_stream(encoder.index, pdecoder) ||
                nullptr == pdecoder)
                return false;

            DecoderCodecContextDefinition& decoder = *pdecoder;

            if (!encoder.options->transcode)
            {
                packet_ptr& output_packet = encoder.packet;
                packet_unref(output_packet);

                av_init_packet(output_packet.get());
                av_packet_ref(output_packet.get(), data_unit.packet.get());

                output_packet->stream_index = encoder.avstream->index;

                av_packet_rescale_ts(output_packet.get(),
                                     decoder.avstream->time_base,
                                     encoder.avstream->time_base);

                encoder.duration = 1000 * double(output_packet->dts) /
                                    double(encoder.avstream->time_base.den) * double(encoder.avstream->time_base.num);

                if (0 != av_interleaved_write_frame(avformat_context.get(),
                                                    output_packet.get()))
                {
                    //logging("error while copying stream packet");
                    return false;
                }
            }
        }
    }

    if (data_unit.more_write_frame || flush)
    {
        for (auto& encoder : definitions)
        {
            if (false == flush &&
                data_unit.stream_index != encoder.index)
                continue;

            DecoderCodecContextDefinition* pdecoder = nullptr;
            if (false == decoder_context.codec_context_definition_by_stream(encoder.index, pdecoder) ||
                nullptr == pdecoder)
                return false;

            DecoderCodecContextDefinition& decoder = *pdecoder;

            if (encoder.options->transcode)
            {
                if (encoder.avmedia_type == AVMEDIA_TYPE_VIDEO)
                    data_unit.frame->pict_type = AV_PICTURE_TYPE_NONE;

                frame_unref(encoder.frame);
                av_frame_ref(encoder.frame.get(), data_unit.frame.get());

                if (encoder.filter_context_sink &&
                    encoder.filter_context_source)
                {
                    //  video example shows AV_BUFFERSRC_FLAG_KEEP_REF instead of 0 below
                    if (0 > av_buffersrc_add_frame_flags(encoder.filter_context_source,
                                                         flush ? nullptr : encoder.frame.get(),
                                                         0))//AV_BUFFERSRC_FLAG_PUSH))
                    {
                        //av_log(nullptr, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
                        return false;
                    }
                }

                if (encoder.filter_context_sink &&
                    encoder.filter_context_source)
                {
                    // pull filtered frame from the filtergraph
                    while (true)
                    {
                        frame_unref(encoder.frame);
                        //int response = av_buffersink_get_frame_flags(encoder.filter_context_sink,
                        //                                             encoder.frame.get(),
                        //                                             AV_BUFFERSINK_FLAG_NO_REQUEST);
                        int response = av_buffersink_get_frame(encoder.filter_context_sink,
                                                               encoder.frame.get());
                        if (response == AVERROR(EAGAIN) ||
                            response == AVERROR_EOF)
                        {
                            if (flush)
                            {
                                encoder.frame.reset();
                                encoder.process_encode_frame(avformat_context,
                                                             decoder);
                            }
                            break;
                        }
                        else if (response < 0)
                            return false;
                        else
                        {
                            encoder.process_encode_frame(avformat_context,
                                                         decoder);
                        }
                    }
                }
                else
                {
                    if (flush)
                        encoder.frame.reset();
                    encoder.process_encode_frame(avformat_context,
                                                 decoder);
                }
            }

            if (flush)
                encoder.avcodec_context.reset();
        }
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

bool transcoder::init(vector<pair<AdminModel::MediaTypeDescriptionVariant, size_t>>& options)
{
    if (false == pimpl->decoder.load(input_file.string()))
        return false;

    size_t option_index = 0;
    for (auto& option : options)
    {
        EncoderContext encoder_context;

        if (false == encoder_context.load(option_index,
                                          option.first,
                                          pimpl->decoder,
                                          output_dir))
            return false;

        if (false == encoder_context.definitions.empty())
            pimpl->encoders.push_back(std::move(encoder_context));

        ++option_index;
    }

    state = before_loop;

    return true;
}

unordered_map<size_t, cloudy::work_unit> transcoder::loop()
{
    unordered_map<size_t, cloudy::work_unit> result;
    bool code = true;

    DataUnit data_unit;
    data_unit.more_read_packet = true;

    // may want to check if there are encoder_context.definitions
    // at all. and skip the whole decoding if there aren't any encoders

    while (true)
    {   //  for now this will not actually do chunk by chunk encoding
        if (false == pimpl->decoder.next(pimpl->encoders, data_unit))
        {
            code = false;
            break;
        }

        for (auto& encoder_context : pimpl->encoders)
        {
            if (false == encoder_context.process(pimpl->decoder,
                                                 data_unit))
            {
                code = false;
                break;
            }

            if (false == data_unit.more_read_frame &&
                false == data_unit.more_read_packet)
            {   //  on flush
                auto& result_item = result[encoder_context.option_index];
                result_item.duration = encoder_context.definitions.front().duration;
                result_item.result_type = InternalModel::ResultType::file;
                result_item.data_or_file = encoder_context.filepath;
            }
        }

        data_unit.more_write_frame = false;
        data_unit.more_write_packet = false;

        if (false == data_unit.more_read_packet)
            break;
    }

    if (false == code)
        result.clear();

    return result;
}

bool transcoder::clean()
{
    pimpl->decoder.avformat_context.reset();

    for (auto& decoder : pimpl->decoder.definitions)
        decoder.avcodec_context.reset();

    return true;
}

unordered_map<size_t, cloudy::work_unit>
transcoder::run()
{
    unordered_map<size_t, cloudy::work_unit> result;
    if (before_loop == state)
    {
        result = loop();
        state = done;
    }
    if (done == state)
        clean();

    return result;
}
}
