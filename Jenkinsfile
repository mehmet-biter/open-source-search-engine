#!/usr/bin/env groovy

node {
	stage 'Checkout'
	checkout([
		$class: 'GitSCM',
		branches: scm.branches,
		doGenerateSubmoduleConfigurations: true,
		extensions: scm.extensions + [[$class: 'SubmoduleOption', parentCredentials: true]],
		userRemoteConfigs: scm.userRemoteConfigs
	])

	stage 'Build'
	sh "make -j4"
}
