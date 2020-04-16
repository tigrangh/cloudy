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
1. HTTP server needs to support Range requests
1. Video transcoder settings need tweaks and fixes for web compatibility.
1. libav wrapper needs fixes. I'm new to this.
1. Media library editing features such as delete, maybe rename, and maybe tagging
1. Media file urls to include cryptographic signature to get verified before serving the file. And an admin interface that creates such urls.
1. Extended support of other media types such as image, pdf, audio.
1. Admin API enhancements to define the types and options of media for the scan.
1. Static front-end generator, maybe.

## dependencies
[boost](https://www.boost.org "boost"), [crypto++](https://www.cryptopp.com/ "crypto++") and [libav](https://www.libav.org/ "libav") are external dependencies.  
[mesh.pp](https://github.com/publiqnet/mesh.pp "mesh.pp"), [belt.pp](https://github.com/publiqnet/belt.pp "belt.pp") and [a simple cmake utility](https://github.com/publiqnet/cmake_utility "the simple title for the simple cmake utility") are git submodules.

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
user@pc:~/projects/cloudy.build$ cmake -DCMAKE_INSTALL_PREFIX=./install -DCMAKE_BUILD_TYPE=Release ../publiq.pp
user@pc:~/projects/cloudy.build$ cmake --build . --target install
```

## Running the server
```console
user@pc:~/projects/cloudy.build/install$ ./bin/cloudy/cloudyd -a 127.0.0.1:4444 -s 0.0.0.0:4445 -d ~/cloudy.datadir
```
-s is the storage endpoint, intended to be exposed to the web  
-a is the admin endpoint, intended for internal use  
-d path to data directory  

## Minimal using example

### Add a file to media library
Consider `/path/to/media/file.mp4` to be a local video file
```console
user@pc:~$ curl -X PUT --data "" "127.0.0.1:4444/library/path/to/media/file.mp4"
{"rtt":5,"files":[],"directories":[]}
```
The response shows already existing files and folders in the library, in this case nothing yet.
This is an asyncronous request.

### Check to know when the video is processed
```console
user@pc:~$ curl "127.0.0.1:4444/log"
{"rtt":10,"log":[{"rtt":12,"path":["path","to","media","file.mp4"]}]}
```
The array "log" will be empty unless the video transcoding is over. When it's done we have the log entry as in the example above.

### So, what is done actually?
```console
user@pc:~$ curl "127.0.0.1:4444/library/path/to/media"
{"rtt":5,"files":[{"rtt":6,"name":"file.mp4","checksums":["GvN8WbnpBtXe6GzJPbQtmanD6gxg7Bt8XHibwU7x546m"]}],"directories":[]}
```
With this we get the checksum of the file - sha256 hash
And then
```console
user@pc:~$ curl "127.0.0.1:4444/index/GvN8WbnpBtXe6GzJPbQtmanD6gxg7Bt8XHibwU7x546m"
{"rtt":23,"paths":["/path/to/media/file"],"media_definition":{"rtt":16,"types_definitions":{"eyJydHQiOjIyLCJhdWRpbyI6eyJydHQiOjIxLCJ0cmFuc2NvZGUiOnsicnR0IjoxOCwiY29kZWMiOiJhYWMiLCJjb2RlY19wcml2X2tleSI6IiIsImNvZGVjX3ByaXZfdmFsdWUiOiIifSwiZmlsdGVyIjp7fX0sInZpZGVvIjp7InJ0dCI6MjEsInRyYW5zY29kZSI6eyJydHQiOjE4LCJjb2RlYyI6ImxpYngyNjQiLCJjb2RlY19wcml2X2tleSI6IngyNjQtcGFyYW1zIiwiY29kZWNfcHJpdl92YWx1ZSI6ImtleWludD02MDptaW4ta2V5aW50PTYwOnNjZW5lY3V0PTA6Zm9yY2UtY2ZyPTEifSwiZmlsdGVyIjp7InJ0dCI6MjAsImhlaWdodCI6MTA4MCwid2lkdGgiOjE5MjAsImZwcyI6MzB9fSwibXV4Ijp7fX0=":{"rtt":17,"type":{"rtt":22,"audio":{"rtt":21,"transcode":{"rtt":18,"codec":"aac","codec_priv_key":"","codec_priv_value":""},"filter":{}},"video":{"rtt":21,"transcode":{"rtt":18,"codec":"libx264","codec_priv_key":"x264-params","codec_priv_value":"keyint=60:min-keyint=60:scenecut=0:force-cfr=1"},"filter":{"rtt":20,"height":1080,"width":1920,"fps":30}},"mux":{}},"sequence":{"rtt":14,"frames":[{"rtt":15,"count":1,"uri":"2abSXktkdeFWBTpJGFXvxT6x5nuPn1MAX9xKD2GPQqv9"}]}},"eyJydHQiOjIyLCJhdWRpbyI6eyJydHQiOjIxLCJ0cmFuc2NvZGUiOnt9LCJmaWx0ZXIiOnt9fSwidmlkZW8iOnsicnR0IjoyMSwidHJhbnNjb2RlIjp7InJ0dCI6MTgsImNvZGVjIjoibGlieDI2NSIsImNvZGVjX3ByaXZfa2V5IjoieDI2NS1wYXJhbXMiLCJjb2RlY19wcml2X3ZhbHVlIjoia2V5aW50PTYwOm1pbi1rZXlpbnQ9NjA6c2NlbmVjdXQ9MCJ9LCJmaWx0ZXIiOnsicnR0IjoyMCwiaGVpZ2h0Ijo3MjAsIndpZHRoIjoxMjgwLCJmcHMiOjMwfX0sIm11eCI6e319":{"rtt":17,"type":{"rtt":22,"audio":{"rtt":21,"transcode":{},"filter":{}},"video":{"rtt":21,"transcode":{"rtt":18,"codec":"libx265","codec_priv_key":"x265-params","codec_priv_value":"keyint=60:min-keyint=60:scenecut=0"},"filter":{"rtt":20,"height":720,"width":1280,"fps":30}},"mux":{}},"sequence":{"rtt":14,"frames":[{"rtt":15,"count":1,"uri":"C56jZnpinpaeS5KDGxtuBRRy3YxcbXx46eFpkgBC1XW4"}]}}}}}
```
This shows that there are two transcoded versions of the original video file, details about the transcoding options and the "uri" of each transcoded file, which can be used to request the file from storage server.

### Get the media library file
```console
user@pc:~$ wget "0.0.0.0:4445/storage?file=2abSXktkdeFWBTpJGFXvxT6x5nuPn1MAX9xKD2GPQqv9"
user@pc:~$ wget "0.0.0.0:4445/storage?file=C56jZnpinpaeS5KDGxtuBRRy3YxcbXx46eFpkgBC1XW4"
```

The JSON structures here are most probably early version and will get modified.
