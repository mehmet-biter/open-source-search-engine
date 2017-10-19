#!/usr/bin/env groovy

pipeline {
	agent any

	options {
		skipDefaultCheckout()
	}

	environment {
		GB_DIR = 'open-source-search-engine'
		PYWEBTEST_DIR = 'pywebtest'
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
					'pywebtest': {
						checkout([
							$class: 'GitSCM',
							branches: [[name: '*/master']],
							doGenerateSubmoduleConfigurations: false,
							extensions: [[$class: 'RelativeTargetDirectory',
							              relativeTargetDir: "${env.PYWEBTEST_DIR}"]] +
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
				parallel(
					'unit test': {
						sh "cd ${env.GB_DIR} && make -j8 unittest"
					},
					'system test (single)': {
						sh "cd ${env.PYWEBTEST_DIR} && ./run_all_testcases.py --num-instances=1 --num-shards=1 --offset=0"
					},
					'system test (multiple)': {
						sh "cd ${env.PYWEBTEST_DIR} && ./run_all_testcases.py --num-instances=4 --num-shards=2 --offset=1"
					}
				)
			}
			post {
				always {
					step([$class: 'XUnitPublisher',
					      thresholds: [[$class: 'FailedThreshold', unstableThreshold: '0']],
					      tools: [[$class: 'GoogleTestType', pattern: '**/test_detail.xml']]])
					step([$class: 'XUnitPublisher',
					      thresholds: [[$class: 'FailedThreshold', unstableThreshold: '0']],
					      tools: [[$class: 'JUnitType', pattern: "${env.PYWEBTEST_DIR}/output.xml"]]])
				}
			}
		}
	}

	post {
		changed {
			script {
				if (currentBuild.result == "SUCCESS" && env.BUILD_NUMBER != 1) {
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
