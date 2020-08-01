# IAVFExtract
Tool for extracting video and audio streams from Take Two Interactive's proprietary IAVF format (used for cutscenes in [Ripper (1996)](https://en.wikipedia.org/wiki/Ripper_(video_game)))

**NOTE: This tool is INCOMPLETE and has no guarantee of working. Please use with caution.**

## About

While a lot of Ripper's content is in fairly standard formats (mostly consisting of SMK, WAV, and BBM), the cutscenes, which naturally take up a majority of the content as an FMV game, is in a proprietary format. While the extension is `.AVI`, it has nothing to do with the common Microsoft AVI format. These files identify themselves as IAVF (presumably for Interleaved Audio Video Format?) and appear to be exclusively used by Take Two Interactive.

The format itself appears to be merely a container - the video/audio streams inside are actually standard Smacker and PCM. This tool is designed to extract Smacker and WAVE audio into standard files that can be played and/or used elsewhere.

## Building

IAVFExtract is written in C and uses no third party dependencies so any standard C compiler should be able to compile it.

```
gcc main.c -o iavfextract
```

CMake is also provided, however it's probably overkill for this sort of thing.

## Usage

```
iavfextract [--verbose] <path-to-file>
```

`--verbose` - Print extra debugging information during extraction. For most use cases this is unnecessary.

`<path-to-file>` - path to the .AVI IAVF file to extract from.

The tool will extract one or more SMK and WAV file from 

## Known Issues

- Not all files extract. Several extract without issues at all, but some contain control codes that are yet to be reversed, and some files seem to use different header structures entirely.
- The extracted videos appear to play out of sync with the audio. I have no idea why this is, and it's not consistent - it seems the video stream either runs too quickly or too slowly. There are no missing/extraneous frames according to the SMK header. Perhaps the IAVF format contains more timing information somewhere?
