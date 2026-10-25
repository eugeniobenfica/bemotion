#!/usr/bin/env bash
# Downloads, once, the speech-to-text model bemotion ships with (whisper.cpp's large-v3-turbo,
# 5-bit: about 550 MB, close to the best accuracy at a fraction of the time) and the small
# voice-activity model that cuts the audio at pauses. They go to ~/bemotion-deps/models,
# beside Skia; the package scripts (tools/package-*.sh) put them in the package
# (assets/models), and run.bat points the development build at them (elsewhere:
# BEMOTION_MODELS=~/bemotion-deps/models). Without them, bemotion offers to download the model
# itself (to its app data folder) the first time speech is transcribed.
#
#   tools/fetch-speech-model.sh [folder]
set -euo pipefail

dir="${1:-${BEMOTION_MODELS:-$HOME/bemotion-deps/models}}"
mkdir -p "$dir"

fetch() { # url file min-bytes
    local url="$1" file="$dir/$2" min="$3"
    if [ -f "$file" ] && [ "$(wc -c < "$file")" -ge "$min" ]; then # (wc: GNU and BSD alike)
        echo "have $2"
        return
    fi
    echo "downloading $2 ..."
    curl -L --fail --retry 3 -o "$file.part" "$url"
    if [ "$(wc -c < "$file.part")" -lt "$min" ]; then
        echo "$2 looks incomplete"
        rm -f "$file.part"
        exit 1
    fi
    mv "$file.part" "$file"
}

fetch https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin ggml-large-v3-turbo-q5_0.bin 400000000
fetch https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin ggml-silero-v5.1.2.bin 500000
echo "Speech models in $dir"
