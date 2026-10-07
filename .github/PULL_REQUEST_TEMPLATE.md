## Summary

<!-- What changed, and why. -->

## Checks

- [ ] `cmake -S . -B build -DOPINION_CLIENT_BUILD_EXAMPLES=ON -DOPINION_CLIENT_BUILD_TESTS=ON`
- [ ] `cmake --build build --parallel`
- [ ] `ctest --test-dir build --output-on-failure`

Title format: `type: description` or `type(scope): description`.
