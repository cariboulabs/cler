# GitHub Actions Workflows

## Overview

This directory contains GitHub Actions workflows that automate testing, building, and deployment processes for the Cler project. Workflows are designed to ensure code quality and cross-platform compatibility.

## Workflow Files

### ci.yml
**Purpose**: Main continuous integration pipeline  
**Triggers**: 
- Push to main/develop branches
- Pull requests
- Nightly scheduled runs (2 AM UTC)
- Manual dispatch with debug options

**What it does**:
- Builds project on multiple platforms (Linux, Windows, macOS)
- Tests with different compilers (GCC, Clang, MSVC, MinGW)
- Runs all automated tests including virtual memory tests
- Performs performance benchmarks
- Uploads test results and artifacts

### Future Workflows
Additional specialized workflows may be added for:
- Release builds and deployment
- Security scanning (CodeQL)
- Documentation generation

## Configuration Details

### Build Matrix
The CI uses a matrix strategy to test across:
- **Operating Systems**: Ubuntu, Windows, macOS (latest versions)
- **Compilers**: GCC 11+, Clang 14+, MSVC 2019+, MinGW
- **Build Types**: Debug and Release configurations

### Environment Variables
```yaml
CMAKE_BUILD_PARALLEL_LEVEL: 4     # Parallel build jobs
CTEST_PARALLEL_LEVEL: 4          # Parallel test execution
CTEST_OUTPUT_ON_FAILURE: ON      # Show output for failed tests
```

### Caching Strategy
- Build dependencies are cached to speed up CI runs
- Cache keys include OS, compiler, and build type
- CMakeLists.txt changes invalidate cache

### Artifacts
The workflow uploads:
- Test results in Testing/ directory
- Build artifacts on failure for debugging
- Performance benchmark results

## Maintenance Guidelines

### Adding New Workflows
1. Create new `.yml` file in this directory
2. Follow naming convention: `purpose.yml`
3. Use consistent job naming
4. Include appropriate triggers
5. Document in this README

### Updating Existing Workflows
1. Test changes in a feature branch first
2. Use workflow_dispatch for manual testing
3. Monitor for deprecation warnings
4. Keep workflows DRY using:
   - Composite actions for repeated steps
   - Reusable workflows for common patterns
   - Matrix strategies for variations

### Best Practices
- **Fail Fast**: Set `fail-fast: false` for matrix builds to see all failures
- **Timeouts**: Add job timeouts to prevent hanging builds
- **Concurrency**: Use concurrency groups to cancel outdated runs
- **Secrets**: Never hardcode sensitive data, use GitHub secrets
- **Debugging**: Include tmate action for interactive debugging

### Performance Optimization
- Use job dependencies to parallelize independent tasks
- Cache expensive operations (dependencies, build artifacts)
- Run heavy tests only on main branch or schedule
- Use `if` conditions to skip unnecessary steps

## Debugging Workflow Failures

### Local Reproduction
1. Check the workflow run logs in Actions tab
2. Note the exact compiler versions and flags used
3. Reproduce the environment locally:
   ```bash
   # Example for Ubuntu GCC build
   export CC=gcc-11
   export CXX=g++-11
   cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
   cmake --build build
   ctest --output-on-failure
   ```

### Interactive Debugging
For difficult issues, trigger a debug session:
1. Go to Actions tab
2. Select workflow and "Run workflow"
3. Check "Enable debug mode with tmate"
4. SSH into the runner when session starts

### Common Issues
- **Compiler not found**: Check available versions on runner images
- **Missing dependencies**: Update apt/brew/choco install commands
- **Test timeouts**: Increase timeout or optimize test
- **Cache misses**: Review cache key strategy

## Security Considerations

### Workflow Permissions
- Use least privilege principle
- Limit `GITHUB_TOKEN` permissions
- Review third-party actions before use
- Pin actions to specific versions/commits

### Pull Request Safety
- External PRs run with read-only permissions
- No secrets available to PR from forks
- Use pull_request_target carefully
- Require approval for first-time contributors

## Monitoring and Alerts

### Workflow Status
- Check Actions tab for run history
- Enable notifications for failures
- Use status badges in README
- Monitor workflow run duration trends

### Cost Management
- GitHub Actions usage included in plan
- Monitor minute usage in Settings
- Optimize long-running workflows
- Use self-hosted runners for heavy workloads

## Resources

- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Runner Images](https://github.com/actions/runner-images)
- [Workflow Syntax](https://docs.github.com/en/actions/using-workflows/workflow-syntax-for-github-actions)
- [Building and Testing C++](https://docs.github.com/en/actions/automating-builds-and-tests/building-and-testing-cpp)