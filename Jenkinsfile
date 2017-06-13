#!/usr/bin/env groovy

node {
	stage 'Checkout'
	checkout scm

	stage 'Build'
	sh "make -j4"
}
