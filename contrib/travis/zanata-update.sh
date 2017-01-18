#!/bin/bash

if echo "$TRAVIS_BRANCH" |egrep -q '^master$|^nm-[0-9]*-[0-9]*$'; then
	wget -O zanata-cli-4.0.0.tar.gz 'http://search.maven.org/remotecontent?filepath=org/zanata/zanata-cli/4.0.0/zanata-cli-4.0.0-dist.tar.gz'
	tar xzf zanata-cli-4.0.0.tar.gz

	make -C po update-po

	zanata-cli-4.0.0/bin/zanata-cli -B push --key "$ZANATA_KEY" --username lkundrak \
	                                --project-version "$TRAVIS_BRANCH"              \
	                                 --url https://translate.zanata.org/zanata/
fi
