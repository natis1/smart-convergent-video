# SCV

Smart convergent video is a simple but powerful tool for benchmarking aomenc. It can help you find the ideal encoding settings given a set of inputs, it can also generate CSVs designed to be imported into R and other statistical programs. It only works on Linux for now because of some hacky system() calls.

### Building it

To build it do the following:

0. Install cmake, git, ffmpeg, a recentish version of libaom, and vmaf
```
git clone https://github.com/natis1/smart-convergent-video scv
mkdir -p scv/build
cd scv/build
cmake ..
make -j
```

You can then run scv with `./scv -i input_file`
