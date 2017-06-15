#!/usr/bin/env groovy

pipeline {
	agent any

	options {
		skipDefaultCheckout()
	}

	environment {
		GB_DIR = 'open-source-search-engine'
		WEBSERVER_DIR = 'pywebserver'
		GTEST_OUTPUT = 'xml'
	}

	stages {
		stage('Checkout') {
			steps {
				parallel (
					'open-source-search-engine': {
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
							            [[$class: 'RelativeTargetDirectory', 
							              relativeTargetDir: "${env.GB_DIR}"]] +
							            [[$class: 'CleanBeforeCheckout']],
							userRemoteConfigs: scm.userRemoteConfigs
						])
					},
					'pywebserver': {
						checkout([
							$class: 'GitSCM',
							branches: [[name: '*/master']],
							doGenerateSubmoduleConfigurations: false,
							extensions: [[$class: 'RelativeTargetDirectory',
							              relativeTargetDir: "${env.WEBSERVER_DIR}"]] +
							            [[$class: 'CleanBeforeCheckout']],
							userRemoteConfigs: [[url: 'https://github.com/privacore/pywebtest.git']]
						])
					}
				)
			}
		}

		stage('Build') {
			steps {
				sh "cd ${env.GB_DIR} && make -j8"
			}
		}

		stage('Test') {
			steps {
				sh "cd ${env.GB_DIR} && make -j8 unittest"
			}
			post {
				always {
					step([$class: 'XUnitPublisher',
					      thresholds: [[$class: 'FailedThreshold', unstableThreshold: '0']],
					      tools: [[$class: 'GoogleTestType', pattern: '**/test_detail.xml']]])
					
				}
			}
		}
	}

	post {
		changed {
			script {
				if (currentBuild.result == "SUCCESS") {
					slackSend color: 'good', message: "${env.JOB_NAME} - #${env.BUILD_NUMBER} Back to normal (<${env.BUILD_URL}|Open>)"
				}
			}
		}

		failure {
			slackSend color: 'danger', message: "${env.JOB_NAME} - #${env.BUILD_NUMBER} Failure (<${env.BUILD_URL}|Open>)"
		}

		unstable {
			slackSend color: 'warning', message: "${env.JOB_NAME} - #${env.BUILD_NUMBER} Unstable (<${env.BUILD_URL}|Open>)"
		}
	}
}
