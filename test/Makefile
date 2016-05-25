.DEFAULT_GOAL := test

.PHONY: all
test: unittest systemtest

.PHONY: unittest
unittest:
	$(MAKE) -C unit test

.PHONY: systemtest
systemtest:
	$(MAKE) -C system test

.PHONY: clean
clean:
	$(MAKE) -C unit $@
	$(MAKE) -C system $@
