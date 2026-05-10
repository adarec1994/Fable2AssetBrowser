FFmpeg drop-in for the asset browser
====================================

This directory holds the FFmpeg static libraries used for in-app XMA2
audio decoding. Without them, the audio player only plays uncompressed
PCM WAVs (most Fable 2 audio is XMA2-compressed).

The libs are linked statically into the exe; there is no DLL sidecar at
runtime.

Expected layout
---------------

    include/ffmpeg/
        include/
            libavcodec/avcodec.h
            libavutil/...
            libswresample/swresample.h
        lib/
            libavcodec.lib       (static)
            libavutil.lib        (static)
            libswresample.lib    (static)

That's it — only the three static libs we actually link, and only the
three header subdirs they require. Earlier versions of the repo also
held the DLL import libs, the DLLs themselves, libavformat/libavfilter/
libavdevice/libswscale/libpostproc, and ~135 MB of FFmpeg PDB files;
those were all dropped when we switched to static linking. Don't re-add
them unless you need a feature beyond XMA2 decode.

How to populate it (Win32, MSVC)
--------------------------------

Easiest source: ShiftMediaProject's prebuilt MSVC libraries.

    https://github.com/ShiftMediaProject/FFmpeg/releases

Download the latest release matching your MSVC version, choosing the
"libffmpeg_<version>_msvc<NN>_x86.zip" archive for x86 (the project
builds 32-bit). After extracting, the layout looks roughly like:

    libffmpeg_.../
        include/
            libavcodec/...   libavutil/...   libswresample/...
            (and others we don't use)
        lib/x86/
            libavcodec.lib   libavutil.lib   libswresample.lib
            (and others we don't use, plus DLL import libs)

Copy `include/libavcodec`, `include/libavutil`, `include/libswresample`
under this directory's `include/`, and copy `libavcodec.lib`,
`libavutil.lib`, `libswresample.lib` from `lib/x86/` under this
directory's `lib/`.

The CRT contract is "/MD" (Release CRT). The repo's CMakeLists.txt
forces the app onto /MD globally so it links cleanly against these
libs in every configuration; don't switch the FFmpeg libs to /MT or
/MDd without rebuilding them yourself.

After populating the directory, re-run CMake configure. You should see:

    -- FFmpeg found at .../include/ffmpeg -- enabling XMA2 decoder (static link)

Why not bundle it?
------------------

FFmpeg static libs are LGPL-licensed and the avcodec.lib alone is
~144 MB on disk (despite the linker only pulling a few MB into the
exe). That's too big to commit; it's left out of source control to
keep the repo cloneable.
