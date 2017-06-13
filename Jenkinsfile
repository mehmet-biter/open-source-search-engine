#!/usr/bin/env groovy

node {
	stage('Checkout') {
		checkout([
			$class: 'GitSCM',
			branches: scm.branches,
			doGenerateSubmoduleConfigurations: false,
			extensions: scm.extensions +
			            [[$class: 'SubmoduleOption',
			              disableSubmodules: false,
			              parentCredentials: false,
			              recursiveSubmodules: true,
			              reference: '',
			              trackingSubmodules: false]] +
			            [[$class: 'CleanBeforeCheckout']],
			userRemoteConfigs: scm.userRemoteConfigs
		])
	}

	stage('Build') {
		sh "make -j4 dist"
	}
}
