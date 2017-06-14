#!/usr/bin/env groovy

pipeline {
	agent any

	options {
		skipDefaultCheckout()
	}

	stages {
		stage('Checkout') {
			steps {
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
		}

		stage('Build') {
			steps {
				sh "make -j4 dist"
			}
		}

		stage('Test') {
			steps {
				sh "GTEST_OUTPUT='xml' make unittest"
			}
			post {
				always {
					step([$class: 'XUnitPublisher',
					      thresholds: [[$class: 'FailedThreshold', failureThreshold: '0']],
					      tools: [[$class: 'GoogleTestType', pattern: '**/test_detail.xml']]])
					
				}
			}
		}
	}

	post {
		//changed {
		//}

		failure {
			slackSend color: 'danger', message: "${env.JOB_NAME} - #${env.BUILD_NUMBER} Failure (<${env.BUILD_URL}|Open>)"
		}

		success {
			slackSend color: 'good', message: "${env.JOB_NAME} - #${env.BUILD_NUMBER} Success (<${env.BUILD_URL}|Open>)"
		}

		unstable {
			slackSend color: 'warning', message: "${env.JOB_NAME} - #${env.BUILD_NUMBER} Unstable (<${env.BUILD_URL}|Open>)"
		}
	}
}
