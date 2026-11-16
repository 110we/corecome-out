CC ?= cc
AR ?= ar
BUILD ?= build
CPPFLAGS := -Ipolicy -Iprofile
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
LIB := $(BUILD)/libredlab.a
OBJECTS := $(BUILD)/redlab_policy.o $(BUILD)/privacy_profile.o

.PHONY: all test clean
all: $(LIB)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/redlab_policy.o: policy/redlab_policy.c policy/redlab_policy.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/privacy_profile.o: profile/privacy_profile.c profile/privacy_profile.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)

$(BUILD)/redlab_policy_test: policy/redlab_policy.c policy/redlab_policy.h tests/policy/redlab_policy_test.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCOXVBN_REDLAB_TESTING policy/redlab_policy.c tests/policy/redlab_policy_test.c -o $@

$(BUILD)/privacy_profile_test: profile/privacy_profile.c profile/privacy_profile.h tests/profile/privacy_profile_test.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) profile/privacy_profile.c tests/profile/privacy_profile_test.c -o $@

test: $(BUILD)/redlab_policy_test $(BUILD)/privacy_profile_test
	$(BUILD)/redlab_policy_test
	$(BUILD)/privacy_profile_test

clean:
	rm -rf $(BUILD)
