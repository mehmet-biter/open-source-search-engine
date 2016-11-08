#!/bin/sh

# Exit on failure
set -e

installBuildWrapper() {
	export SONAR_HOME=$HOME/.sonar
	mkdir -p $SONAR_HOME
	curl -sSLo $SONAR_HOME/build-wrapper-linux-x86.zip https://sonarqube.com/static/cpp/build-wrapper-linux-x86.zip
	unzip $SONAR_HOME/build-wrapper-linux-x86.zip -d $SONAR_HOME
	rm $SONAR_HOME/build-wrapper-linux-x86.zip
	export PATH=$SONAR_HOME/build-wrapper-linux-x86:$PATH
}

# Install the build-wrapper
installBuildWrapper

make clean

# make using build-wrapper
build-wrapper-linux-x86-64 --out-dir sonar-out make

# And run the analysis
sonar-scanner -Dsonar.login=$SONAR_TOKEN

