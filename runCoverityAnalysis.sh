#!/bin/sh

# Exit on failure
set -e

if [ "${TRAVIS_JOB_NUMBER##*.}" != "1" ] || \
   [ "${TRAVIS_PULL_REQUEST}" = "true" ]
then
  echo "Skipping coverity scan."
  exit
fi

# setup parameters
export COVERITY_SCAN_PROJECT_NAME="os+search+engine"
export COVERITY_SCAN_NOTIFICATION_EMAIL="br@privacore.com"
export COVERITY_SCAN_BUILD_COMMAND_PREPEND="make clean && make libcld2_full.so slacktee.sh"
export COVERITY_SCAN_BUILD_COMMAND="make"
export COVERITY_SCAN_BRANCH_PATTERN="master"

curl -s "https://scan.coverity.com/scripts/travisci_build_coverity_scan.sh" | bash || :

