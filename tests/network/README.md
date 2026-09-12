# iOS network integration checks

These checks compile the game's real `src/libs/network.cpp` as an isolated iOS
Simulator executable. They use a loopback fixture, a fixed test-only signing key,
and in-memory player configuration. They never read game saves or production
credentials. The optional target is excluded from ordinary builds.

In one terminal, start the fixture (restart it before each test run):

```sh
python3 tests/network/server.py
```

In another terminal, configure an isolated test build so the game's usual network
configuration is preserved. This reuses the Simulator FFmpeg prefix created by
`build_ios.sh simulator`:

```sh
BUILD_DIR="$PWD/build-ios-network-tests" ./build_ios.sh simulator \
  -DNETWORK_URL=http://127.0.0.1:18765 \
  -DNETWORK_AUTH_KEY=yataidon-integration-test \
  -DYATAIDON_BUILD_NETWORK_TESTS=ON
cmake --build build-ios-network-tests --config Release --target network_integration \
  -- CODE_SIGNING_ALLOWED=NO
xcrun simctl spawn booted \
  "$PWD/build-ios-network-tests/tests/network/Release-iphonesimulator/network_integration"
```

Boot one iOS Simulator before the last command. The executable exits with 0 on
success and 1 on failure. HTTPS probes require internet access to `www.apple.com`
and `self-signed.badssl.com`; a connection failure is a failed test, not proof of
certificate rejection.

Coverage: disabled networking; authenticated health, registration and import flag
requests; Unicode/form encoding; profile/title/colors/costume read/write; delayed
asynchronous score upload with input log and modifiers; score download; consuming
a remote song jump; minimum-version heartbeat; server failure and reconnection;
trusted HTTPS and rejection of a self-signed certificate.

These are client/transport integration checks. Full game startup persistence,
bulk history sync, real backend compatibility, and physical-device background
behavior still require end-to-end device testing.
