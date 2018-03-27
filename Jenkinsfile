#!/usr/bin/env groovy

pipeline {
	agent any

	options {
		skipDefaultCheckout()
		buildDiscarder(logRotator(artifactNumToKeepStr: '1'))
	}

	environment {
		GB_PROJECT = 'open-source-search-engine'
		PYWEBTEST_PROJECT = 'pywebtest'
		GTEST_OUTPUT = 'xml'
	}

	stages {
		stage('Checkout') {
			steps {
				parallel (
					'open-source-search-engine': {
						dir("${env.GB_PROJECT}") {
							checkout resolveScm(
								source: [
									$class: 'GitSCMSource',
									remote: "https://github.com/privacore/${env.GB_PROJECT}.git",
									traits: [
										[$class: 'jenkins.plugins.git.traits.BranchDiscoveryTrait'],
										[$class: 'CleanBeforeCheckoutTrait'],
										[$class: 'DisableStatusUpdateTrait'],
										[$class: 'SubmoduleOptionTrait', recursiveSubmodules: true]
									]
								],
								targets: ["${env.BRANCH_NAME}"]
							)
						}
					},
					'pywebtest': {
						dir("${env.PYWEBTEST_PROJECT}") {
							checkout resolveScm(
								source: [
									$class: 'GitSCMSource',
									remote: "https://github.com/privacore/${env.PYWEBTEST_PROJECT}.git",
									traits: [
										[$class: 'jenkins.plugins.git.traits.BranchDiscoveryTrait'],
										[$class: 'CleanBeforeCheckoutTrait']
									]
								],
								targets: ["${env.BRANCH_NAME}", 'master']
							)
						}
					}
				)
			}
		}

		stage('Build') {
			steps {
				sh "cd ${env.GB_PROJECT} && make -j8 config=debug dist"
				archiveArtifacts artifacts: "${env.GB_PROJECT}/*.tar.gz", fingerprint: true
			}
		}

		stage('Test (setup)') {
			steps {
				sh "cd ${env.PYWEBTEST_PROJECT} && ./create_ssl_key.sh"
				sh "cd ${env.PYWEBTEST_PROJECT} && ./create_ssl_cert.sh"
				sh "cd ${env.PYWEBTEST_PROJECT} && ./setup_instances.py --num-instances=1 --num-shards=1 --offset=0"
				sh "cd ${env.PYWEBTEST_PROJECT} && ./setup_instances.py --num-instances=4 --num-shards=2 --offset=1"
			}
		}

		stage('Test') {
			steps {
				parallel(
					'unit test': {
						timeout(time: 10, unit: 'MINUTES') {
							sh "cd ${env.GB_PROJECT} && make -j8 unittest"
						}
					},
					'system test (single)': {
						timeout(time: 30, unit: 'MINUTES') {
							sh "cd ${env.PYWEBTEST_PROJECT} && ./run_all_testcases.py --num-instances=1 --num-shards=1 --offset=0"
						}
					},
					'system test (multiple)': {
						timeout(time: 30, unit: 'MINUTES') {
							sh "cd ${env.PYWEBTEST_PROJECT} && ./run_all_testcases.py --num-instances=4 --num-shards=2 --offset=1"
						}
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
					      tools: [[$class: 'JUnitType', pattern: "${env.PYWEBTEST_PROJECT}/output*.xml"]]])
				}
			}
		}
	}

	post {
		always {
			sh "killall -u \$(whoami) -s SIGINT gb || true"
		}

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
