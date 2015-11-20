.DEFAULT_GOAL := test

.PHONY: all
test: unittest systemtest

.PHONY: unittest
unittest:
	make -C unit test

.PHONY: systemtest
systemtest:
	make -C system test

.PHONY: clean
clean:
	make -C unit $@
	make -C system $@
