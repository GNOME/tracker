#!/usr/bin/bash

tracker_target=

mkdir extra
cd extra

git clone https://gitlab.gnome.org/GNOME/tracker-miners.git

if [ $? -ne 0 ]; then
  echo Checkout failed
  exit 1
fi

cd tracker

if [ "$CI_MERGE_REQUEST_TARGET_BRANCH_NAME" ]; then
  merge_request_remote=${CI_MERGE_REQUEST_SOURCE_PROJECT_URL//tracker/tracker-miners}
  merge_request_branch=$CI_MERGE_REQUEST_SOURCE_BRANCH_NAME

  echo Looking for $merge_request_branch on remote ...
  if git fetch -q $merge_request_remote $merge_request_branch 2>/dev/null; then
    tracker_target=FETCH_HEAD
  else
    tracker_target=origin/$CI_MERGE_REQUEST_TARGET_BRANCH_NAME
    echo Using $tracker_target instead
  fi
fi

if [ -z "$tracker_target" ]; then
  tracker_target=$(git branch -r -l origin/$CI_COMMIT_REF_NAME)
  tracker_target=${tracker_target:-origin/master}
  echo Using $tracker_target instead
fi

git checkout -q $tracker_target

