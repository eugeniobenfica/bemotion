#!/usr/bin/env sh
# Shares libs/bestyle with its repository, and through it with every project
# that uses it (bemotion, Artisan…). The code lives in each project as a git subtree:
# builds need nothing extra; changes travel only when you ask.
#
#   tools/bestyle.sh status   what differs between this project and bestyle
#   tools/bestyle.sh pull     bring bestyle's changes in (one squashed commit)
#   tools/bestyle.sh push     send this project's committed changes to bestyle
#
# Edit libs/bestyle in any project, commit, push; then pull in the others.
set -eu

url=https://github.com/eugeniobenfica/bestyle.git
branch=main
prefix=libs/bestyle

cd "$(git rev-parse --show-toplevel)"

clean_prefix() {
    if [ -n "$(git status --porcelain -- "$prefix")" ]; then
        echo "$prefix has uncommitted changes: commit them first." >&2
        exit 1
    fi
}

case "${1:-status}" in
status)
    git fetch -q "$url" "$branch"
    if git diff --quiet FETCH_HEAD "HEAD:$prefix"; then
        echo "$prefix matches bestyle."
    else
        echo "Committed differences (bestyle -> this project):"
        git diff --stat FETCH_HEAD "HEAD:$prefix"
    fi
    git status --short -- "$prefix"
    ;;
pull)
    clean_prefix
    git subtree pull --prefix "$prefix" "$url" "$branch" --squash \
        -m "Update bestyle"
    ;;
push)
    clean_prefix
    git subtree push --prefix "$prefix" "$url" "$branch" ||
        { echo "Push refused: run tools/bestyle.sh pull first, then push again." >&2; exit 1; }
    # Record what was pushed, so the next pull does not merge it again
    git subtree pull --prefix "$prefix" "$url" "$branch" --squash -m "Update bestyle"
    ;;
*)
    echo "usage: tools/bestyle.sh [status|pull|push]" >&2
    exit 2
    ;;
esac
