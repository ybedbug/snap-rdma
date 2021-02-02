#!/bin/bash

RED="\033[31m"
YELLOW="\033[33m"
BLUE="\033[94m"
GREEN="\033[32m"
NO_COLOR="\033[0m"

SCRIPT=$(readlink -f "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

run_git_check(){
    echo -ne "Running gitlint...\n"
    local ret_code=0
    cmn_acstr=$(git merge-base origin/${ghprbTargetBranch:-master} HEAD)
    for commit_sha in $(git log --pretty=%H ${cmn_acstr}..HEAD); do
        echo "Checking commit message ${commit_sha}"
        RESULT=`git log -1 --pretty=%B ${commit_sha} | gitlint -C ${SCRIPTPATH}/.gitlint -e ${SCRIPTPATH} 2>&1`
        local exit_code=$?
        ret_code=$((ret_code + $exit_code))
        handle_test_result $exit_code "$RESULT"
    done
    # return $ret_code
}

handle_test_result(){
    EXIT_CODE=$1
    RESULT="$2"
    # Change color to red or green depending on SUCCESS
    if [ $EXIT_CODE -eq 0 ]; then
        echo -e "${GREEN}SUCCESS"
    else
        echo -e "${RED}FAIL"
    fi
    # Print RESULT if not empty
    if [ -n "$RESULT" ] ; then
        if [ $EXIT_CODE -eq 0 ]; then
            echo -e "\n${YELLOW}$RESULT"
        else
            echo -e "\n${RED}$RESULT"
        fi
    fi
    # Reset color
    echo -e "${NO_COLOR}"
}

run_git_check
