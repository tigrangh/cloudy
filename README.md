# Cloudy

This is a media indexing and streaming server.
For now, the only type of supported media is video.

## Built in are
1. HTTP file server, that is able to serve the media over the web.
1. HTTP/JSON RPC admin API, that allows to add local files to the internal index.
1. Internal persistent storage, used to store the media index, organized using a tree like hierarchical structure and the media files, in particular the transcoded video files.
1. Video transcoder, based on multiple profiles. Any video file has several transcoded versions.

## What this lacks
1. A front-end
1. User management
1. A full HTTP 1.1 server. Instead here is a stripped down, custom implementation.

## Expected enhancements
1. Video transcoder settings can be tweaked and fixed for web compatibility, by changing the request arguments.
1. ffmpeg libav wrapper needs more fixes.
1. Extended support of other media types such as image, pdf, audio.
1. Static front-end generator, maybe.

## Dependencies
[boost](https://www.boost.org "boost"), [crypto++](https://www.cryptopp.com/ "crypto++") and [ffmpeg](https://ffmpeg.org/ "ffmpeg") are external dependencies.  
[mesh.pp](https://github.com/publiqnet/mesh.pp "mesh.pp"), [belt.pp](https://github.com/publiqnet/belt.pp "belt.pp") and [a simple cmake utility](https://github.com/publiqnet/cmake_utility "the simple title for the simple cmake utility") are git submodules.

In my development environment I have `ffmpeg version 4.2.1 Copyright (c) 2000-2019 the FFmpeg developers`, I hope this will compile in your environment too.

## How to build cloudy?
```console
user@pc:~$ mkdir projects
user@pc:~$ cd projects
user@pc:~/projects$ git clone https://github.com/tigrangh/cloudy
user@pc:~/projects$ cd cloudy
user@pc:~/projects/cloudy$ git submodule update --init --recursive
user@pc:~/projects/cloudy$ cd ..
user@pc:~/projects$ mkdir cloudy.build
user@pc:~/projects$ cd cloudy.build
user@pc:~/projects/cloudy.build$ cmake -DCMAKE_INSTALL_PREFIX=./install -DCMAKE_BUILD_TYPE=Release ../cloudy
user@pc:~/projects/cloudy.build$ cmake --build . --target install
```

## Running the server
```console
user@pc:~/projects/cloudy.build/install$ ./bin/cloudy/cloudyd -a 127.0.0.1:4444 -s 0.0.0.0:4445 -d ~/cloudy.datadir
```
-s is the storage endpoint, intended to be exposed to the web  
-a is the admin endpoint, intended for internal use  
-d path to data directory
Use also -k option to define a cryptographic private key. just run once without it, and later reuse the same key that was automatically generated.

## Minimal usage example

### Add a file to media library
Consider `/path/to/media/file.mp4` to be a local video file
```console
user@pc:~$ curl -X PUT --data '[{"rtt":24, "container_extension":"mp4", "muxer_opt_key":"", "muxer_opt_value":"", "audio":{"rtt":25, "transcode":{"rtt":26, "codec":"aac", "codec_priv_key":"", "codec_priv_value":""}}, "video":{"rtt":25, "transcode":{"rtt":26, "codec":"libx264", "codec_priv_key":"x264-params", "codec_priv_value":"keyint=60:min-keyint=60:scenecut=0:force-cfr=1", "filter":{"rtt":27, "height":1080, "width":1920, "fps":29}}}}, {"rtt":24, "container_extension":"mp4", "muxer_opt_key":"", "muxer_opt_value":"", "audio":{"rtt":25, "transcode":{"rtt":26, "codec":"aac", "codec_priv_key":"", "codec_priv_value":""}}, "video":{"rtt":25, "transcode":{"rtt":26, "codec":"libx264", "codec_priv_key":"x264-params", "codec_priv_value":"keyint=60:min-keyint=60:scenecut=0:force-cfr=1", "filter":{"rtt":27, "height":720, "width":1280, "fps":29}}}}, {"rtt":24, "container_extension":"mp4", "muxer_opt_key":"", "muxer_opt_value":"", "audio":{"rtt":25, "transcode":{"rtt":26, "codec":"aac", "codec_priv_key":"", "codec_priv_value":""}}, "video":{"rtt":25, "transcode":{"rtt":26, "codec":"libx264", "codec_priv_key":"x264-params", "codec_priv_value":"keyint=60:min-keyint=60:scenecut=0:force-cfr=1", "filter":{"rtt":27, "height":360, "width":640, "fps":29}}}}]' "127.0.0.1:4444/library/path/to/media/file.mp4"
{"rtt":5,"files":[],"directories":[]}
```
The response shows already existing files and folders in the library (in the current directory), in this case nothing yet.
This is an asyncronous request.

### Check to know when the video is processed
```console
user@pc:~$ curl "127.0.0.1:4444/log"
{"rtt":12,"log":[{"rtt":13,"path":["path","to","media","file.mp4"]}]}
```
The array "log" will be empty unless the waiting for the video transcoding is over. When it's done we have the log entry as in the example above. There are different codes to indicate an error, warning or success. The above example shows success.

### So, what is done actually?
```console
user@pc:~$ curl "127.0.0.1:4444/library/path/to/media"
{"rtt":7,"files":[{"rtt":8,"name":"file.mp4","checksum":"GvN8WbnpBtXe6GzJPbQtmanD6gxg7Bt8XHibwU7x546m"}],"directories":[]}
```
With this we get the checksum of the file - sha256 hash  
And then
```console
user@pc:~$ curl "127.0.0.1:4444/index/GvN8WbnpBtXe6GzJPbQtmanD6gxg7Bt8XHibwU7x546m"
{"rtt":22,"paths":[["path","to","media","file.mp4"]],"type_definitions":[{"rtt":21,"type_description":{"rtt":24,"audio":{"rtt":25,"transcode":{"rtt":26,"codec":"aac","codec_priv_key":"","codec_priv_value":""}},"video":{"rtt":25,"transcode":{"rtt":26,"codec":"libx264","codec_priv_key":"x264-params","codec_priv_value":"keyint=60:min-keyint=60:scenecut=0:force-cfr=1","filter":{"rtt":27,"height":360,"width":640,"fps":29}}},"muxer_opt_key":"","muxer_opt_value":"","container_extension":"mp4"},"sequence":{"rtt":19,"frames":[{"rtt":20,"count":54213,"uri":"ASCvRY6YCMsLAD2iPyMHPnnb9Lqjg1Zhq15o8JnxYSfM"}]}},{"rtt":21,"type_description":{"rtt":24,"audio":{"rtt":25,"transcode":{"rtt":26,"codec":"aac","codec_priv_key":"","codec_priv_value":""}},"video":{"rtt":25,"transcode":{"rtt":26,"codec":"libx264","codec_priv_key":"x264-params","codec_priv_value":"keyint=60:min-keyint=60:scenecut=0:force-cfr=1","filter":{"rtt":27,"height":1080,"width":1920,"fps":29}}},"muxer_opt_key":"","muxer_opt_value":"","container_extension":"mp4"},"sequence":{"rtt":19,"frames":[{"rtt":20,"count":54213,"uri":"2abSXktkdeFWBTpJGFXvxT6x5nuPn1MAX9xKD2GPQqv9"}]}},{"rtt":21,"type_description":{"rtt":24,"audio":{"rtt":25,"transcode":{"rtt":26,"codec":"aac","codec_priv_key":"","codec_priv_value":""}},"video":{"rtt":25,"transcode":{"rtt":26,"codec":"libx264","codec_priv_key":"x264-params","codec_priv_value":"keyint=60:min-keyint=60:scenecut=0:force-cfr=1","filter":{"rtt":27,"height":720,"width":1280,"fps":29}}},"muxer_opt_key":"","muxer_opt_value":"","container_extension":"mp4"},"sequence":{"rtt":19,"frames":[{"rtt":20,"count":54213,"uri":"C56jZnpinpaeS5KDGxtuBRRy3YxcbXx46eFpkgBC1XW4"}]}}]}
```
This shows that there are three transcoded versions of the original video file, details about the transcoding options and the "uri" of each transcoded file, which can be used to request the file from storage server. By the way, "count" shows the duration of the video in milliseconds.
But just the uri is made to be not enough to get the file, we need to ask the admin interface to sign it, and get an authorization.

### Get the authorization
```console
user@pc:~$ curl "127.0.0.1:4444/authorization?file=C56jZnpinpaeS5KDGxtuBRRy3YxcbXx46eFpkgBC1XW4&seconds=3600"
eyJydHQiOjE3LCJ0b2tlbiI6eyJydHQiOjE2LCJmaWxlX3VyaSI6IkM1NmpabnBpbnBhZVM1S0RHeHR1QlJSeTNZeGNiWHg0NmVGcGtnQkMxWFc0Iiwic2Vzc2lvbl9pZCI6IiIsInNlY29uZHMiOjM2MDAsInRpbWVfcG9pbnQiOiIyMDIwLTA0LTIzIDA5OjAwOjAwIn0sImF1dGhvcml6YXRpb24iOnsicnR0IjoxOCwiYWRkcmVzcyI6IkNsb3VkeS01d21EYlJxVEt3c1ZZSEVjb0Y2blhmSzZGVFZoU2FUZEs1VVMxOURXMmpROW1hNmtOZiIsInNpZ25hdHVyZSI6IjM4MXlYWW5uOUxIYVBwZWVNRmRVczNuU2dSVnFUTVo0bzZUOVpXd2hZWXJzb01TZXBuenJnRHE1Sld2elJCWkxTVnhQVE0xeGhQeVlFbXZveDNxSnllQ3ZVdzdFdW0zaSJ9fQ==
```
This long string is a base64 encoded json structure that includes cryptographic signature and link validity information. We'll use it to get the actual transcoded video file from storage api. Be sure to url encode it properly

### Get the media library file
```console
user@pc:~$ wget "0.0.0.0:4445/storage?authorization=eyJydHQiOjE3LCJ0b2tlbiI6eyJydHQiOjE2LCJmaWxlX3VyaSI6IkM1NmpabnBpbnBhZVM1S0RHeHR1QlJSeTNZeGNiWHg0NmVGcGtnQkMxWFc0Iiwic2Vzc2lvbl9pZCI6IiIsInNlY29uZHMiOjM2MDAsInRpbWVfcG9pbnQiOiIyMDIwLTA0LTIzIDA5OjAwOjAwIn0sImF1dGhvcml6YXRpb24iOnsicnR0IjoxOCwiYWRkcmVzcyI6IkNsb3VkeS01d21EYlJxVEt3c1ZZSEVjb0Y2blhmSzZGVFZoU2FUZEs1VVMxOURXMmpROW1hNmtOZiIsInNpZ25hdHVyZSI6IjM4MXlYWW5uOUxIYVBwZWVNRmRVczNuU2dSVnFUTVo0bzZUOVpXd2hZWXJzb01TZXBuenJnRHE1Sld2elJCWkxTVnhQVE0xeGhQeVlFbXZveDNxSnllQ3ZVdzdFdW0zaSJ9fQ%3D%3D"
```

### Create a simple static html page

We can "upload" any file to cloudy. For example let's have `/path/to/index.html` file with the following content.

```
<video width="800" height="600" controls>
  <source src="http://example.com:4445/storage?authorization=eyJydHQiOjE3LCJ0b2tlbiI6eyJydHQiOjE2LCJmaWxlX3VyaSI6IkM1NmpabnBpbnBhZVM1S0RHeHR1QlJSeTNZeGNiWHg0NmVGcGtnQkMxWFc0Iiwic2Vzc2lvbl9pZCI6IiIsInNlY29uZHMiOjM2MDAsInRpbWVfcG9pbnQiOiIyMDIwLTA0LTIzIDA5OjAwOjAwIn0sImF1dGhvcml6YXRpb24iOnsicnR0IjoxOCwiYWRkcmVzcyI6IkNsb3VkeS01d21EYlJxVEt3c1ZZSEVjb0Y2blhmSzZGVFZoU2FUZEs1VVMxOURXMmpROW1hNmtOZiIsInNpZ25hdHVyZSI6IjM4MXlYWW5uOUxIYVBwZWVNRmRVczNuU2dSVnFUTVo0bzZUOVpXd2hZWXJzb01TZXBuenJnRHE1Sld2elJCWkxTVnhQVE0xeGhQeVlFbXZveDNxSnllQ3ZVdzdFdW0zaSJ9fQ%3D%3D" type="video/mp4">
  Your browser does not support the video tag.
</video>
```

Then
```console
user@pc:~$ curl -X PUT --data '[{"rtt":28, "mime_type":"text/html"}]' "127.0.0.1:4444/library/path/to/index.html"
{"rtt":5,"files":[],"directories":[]}
```

With this we do a similar thing as above when adding a video file to library, but instead of telling cloudy to transcode a video file, we simply ask it to copy this file to internal structure, and remember its mime-type as "text/html".
Following the examples from above steps, we can get the url of this simple html page, and share it with other people or services.

### JSON protocol

The following is not a real JSON schema, but it gives enough information how to tweak the JSON parameters.
This is defined in a built-in small language, which helps to autogenerate whole lot of c++ code during the build process, which is used to actually implement the protocol.

There are also tools to autogenerate php and TypeScript libraries.

```console
user@pc:~$ curl 127.0.0.1:4444/protocol
{

    "IndexListGet": {
        "type": "object",
        "rtt": 0,
        "properties": {
        }
    },

    "IndexListResponse": {
        "type": "object",
        "rtt": 1,
        "properties": {
            "list_index": { "type": "Hash String LibraryIndex"},
        }
    },

    "IndexGet": {
        "type": "object",
        "rtt": 2,
        "properties": {
            "sha256sum": { "type": "String"},
        }
    },

    "IndexDelete": {
        "type": "object",
        "rtt": 3,
        "properties": {
            "sha256sum": { "type": "String"},
        }
    },

    "LibraryGet": {
        "type": "object",
        "rtt": 4,
        "properties": {
            "path": { "type": "Array String"},
        }
    },

    "LibraryPut": {
        "type": "object",
        "rtt": 5,
        "properties": {
            "path": { "type": "Array String"},
            "type_descriptions": { "type": "Array Object"},
        }
    },

    "LibraryDelete": {
        "type": "object",
        "rtt": 6,
        "properties": {
            "path": { "type": "Array String"},
        }
    },

    "LibraryResponse": {
        "type": "object",
        "rtt": 7,
        "properties": {
            "files": { "type": "Array LibraryItemFile"},
            "directories": { "type": "Array LibraryItemDirectory"},
        }
    },

    "LibraryItemFile": {
        "type": "object",
        "rtt": 8,
        "properties": {
            "name": { "type": "String"},
            "checksum": { "type": "String"},
        }
    },



    "LibraryItemDirectory": {
        "type": "object",
        "rtt": 9,
        "properties": {
            "name": { "type": "String"},
        }
    },

    "LogGet": {
        "type": "object",
        "rtt": 10,
        "properties": {
        }
    },

    "LogDelete": {
        "type": "object",
        "rtt": 11,
        "properties": {
            "count": { "type": "UInt64"},
        }
    },

    "Log": {
        "type": "object",
        "rtt": 12,
        "properties": {
            "log": { "type": "Array Object"},
        }
    },

    "CheckMediaResult": {
        "type": "object",
        "rtt": 13,
        "properties": {
            "path": { "type": "Array String"},
        }
    },

    "CheckMediaError": {
        "type": "object",
        "rtt": 14,
        "properties": {
            "path": { "type": "Array String"},
            "reason": { "type": "String"},
        }
    },

    "CheckMediaWarning": {
        "type": "object",
        "rtt": 15,
        "properties": {
            "path": { "type": "Array String"},
            "reason": { "type": "String"},
        }
    },

    "StorageAuthorization": {
        "type": "object",
        "rtt": 16,
        "properties": {
            "file_uri": { "type": "String"},
            "session_id": { "type": "String"},
            "seconds": { "type": "UInt64"},
            "time_point": { "type": "TimePoint"},
        }
    },

    "SignedStorageAuthorization": {
        "type": "object",
        "rtt": 17,
        "properties": {
            "token": { "type": "StorageAuthorization"},
            "authorization": { "type": "Authority"},
        }
    },

    "Authority": {
        "type": "object",
        "rtt": 18,
        "properties": {
            "address": { "type": "String"},
            "signature": { "type": "String"},
        }
    },



    "MediaSequence": {
        "type": "object",
        "rtt": 19,
        "properties": {
            "frames": { "type": "Array MediaFrame"},
        }
    },

    "MediaFrame": {
        "type": "object",
        "rtt": 20,
        "properties": {
            "count": { "type": "UInt64"},
            "uri": { "type": "String"},
        }
    },

    "MediaTypeDefinition": {
        "type": "object",
        "rtt": 21,
        "properties": {
            "type_description": { "type": "Object"},
            "sequence": { "type": "MediaSequence"},
        }
    },

    "LibraryIndex": {
        "type": "object",
        "rtt": 22,
        "properties": {
            "paths": { "type": "Array Array"},
            "type_definitions": { "type": "Array MediaTypeDefinition"},
        }
    },

    "RemoteError": {
        "type": "object",
        "rtt": 23,
        "properties": {
            "message": { "type": "String"},
        }
    },

    "MediaTypeDescriptionVideoContainer": {
        "type": "object",
        "rtt": 24,
        "properties": {
            "audio": { "type": "Optional MediaTypeDescriptionAVStream"},
            "video": { "type": "Optional MediaTypeDescriptionAVStream"},
            "muxer_opt_key": { "type": "String"},
            "muxer_opt_value": { "type": "String"},
            "container_extension": { "type": "String"},
        }
    },

    "MediaTypeDescriptionAVStream": {
        "type": "object",
        "rtt": 25,
        "properties": {
            "transcode": { "type": "Optional MediaTypeDescriptionAVStreamTranscode"},
        }
    },

    "MediaTypeDescriptionAVStreamTranscode": {
        "type": "object",
        "rtt": 26,
        "properties": {
            "codec": { "type": "String"},
            "codec_priv_key": { "type": "String"},
            "codec_priv_value": { "type": "String"},
            "filter": { "type": "Optional MediaTypeDescriptionVideoFilter"},
        }
    },

    "MediaTypeDescriptionVideoFilter": {
        "type": "object",
        "rtt": 27,
        "properties": {
            "height": { "type": "UInt64"},
            "width": { "type": "UInt64"},
            "fps": { "type": "UInt64"},
        }
    },

    "MediaTypeDescriptionRaw": {
        "type": "object",
        "rtt": 28,
        "properties": {
            "mime_type": { "type": "String"},
        }
    }

}
```
