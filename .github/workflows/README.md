# GitHub Actions Workflows

## Overview

This directory contains GitHub Actions workflows that automate testing, building, and deployment processes for the Cler project. Workflows are designed to ensure code quality and cross-platform compatibility.

## Build Environments

### Linux (Docker-based)
- **Reproducible builds** using Docker containers
- **Base image**: Ubuntu 22.04
- **Compilers**: GCC and Clang-14
- **Dependencies**: Pre-installed in container for consistency
- **Isolation**: Each build runs in a clean environment

### macOS (Native)
- **Runner**: GitHub's macos-latest
- **Compiler**: Apple Clang
- **Dependencies**: Installed via Homebrew
- **Architecture**: Supports both Intel and Apple Silicon

### Windows (WSL Recommended)
- **Note**: CLER does not officially support native Windows builds
- **Recommended approach**: Use [Windows Subsystem for Linux (WSL2)](https://docs.microsoft.com/en-us/windows/wsl/install)
- **WSL setup**: WSL provides full Linux compatibility, allowing you to use CLER exactly as you would on Ubuntu/Debian
- **Benefits**: No special Windows toolchain needed, full POSIX support, direct use of Linux build instructions

## Docker Development Environment

### Quick Start
```bash
# From the repository root:
cd docker

# Run tests with GCC
docker compose run test-gcc

# Run tests with Clang
docker compose run test-clang

# Interactive development environment
docker compose run --rm dev

# Build specific targets (from repo root)
docker build -f docker/Dockerfile --target gcc-builder -t cler:gcc .
docker build -f docker/Dockerfile --target clang-builder -t cler:clang .
```

### Docker Targets
- `base`: Minimal build environment
- `deps`: Full dependencies installed
- `gcc-builder`: GCC release build
- `clang-builder`: Clang release build
- `test-runner`: Test execution environment
- `development`: Interactive development with tools

### Local Testing with Docker
```bash
# Run the same tests as CI (from repo root)
docker build -f docker/Dockerfile --target test-runner -t cler:test .
docker run --rm cler:test

# Run with specific compiler
docker run --rm -e CC=clang-14 -e CXX=clang++-14 cler:test
```

## Workflow Files

### ci.yml
**Purpose**: Main continuous integration pipeline  
**Triggers**: 
- Push to main/develop branches
- Pull requests
- Nightly scheduled runs (2 AM UTC)
- Manual dispatch with debug options

**What it does**:
- Builds project on multiple platforms
- Tests with different compilers and configurations
- Runs all automated tests including virtual memory tests
- Performs performance benchmarks
- Uploads test results and artifacts

### Build Matrix
- **Linux**: Docker-based builds with GCC/Clang × Debug/Release
- **macOS**: Native builds with Clang × Debug/Release

## Configuration Details

### Environment Variables
```yaml
CMAKE_BUILD_PARALLEL_LEVEL: 4     # Parallel build jobs
CTEST_PARALLEL_LEVEL: 4          # Parallel test execution
CTEST_OUTPUT_ON_FAILURE: ON      # Show output for failed tests
```

### Caching Strategy
- Docker layer caching for Linux builds
- Build dependency caching for all platforms
- Cache keys include OS, compiler, and build type

### Artifacts
The workflow uploads:
- Test results in Testing/ directory
- Build artifacts on failure for debugging
- Performance benchmark results

## Maintenance Guidelines

### Updating Dependencies

#### Linux (Docker)
Edit the `docker/Dockerfile` to add/update dependencies:
```dockerfile
RUN apt-get update && apt-get install -y \
    new-package-name \
    && rm -rf /var/lib/apt/lists/*
```

#### macOS (Homebrew)
Update the brew install command in `ci.yml`:
```bash
brew install new-package
```

### Adding New Workflows
1. Create new `.yml` file in this directory
2. Follow naming convention: `purpose.yml`
3. Use consistent job naming
4. Include appropriate triggers
5. Document in this README

### Best Practices
- **Reproducibility**: Use Docker for Linux builds
- **Isolation**: Each build in clean environment
- **Fail Fast**: Set `fail-fast: false` to see all failures
- **Timeouts**: Add job timeouts to prevent hanging
- **Concurrency**: Use concurrency groups to cancel outdated runs

## Debugging Workflow Failures

### Local Reproduction

#### Linux (Docker)
```bash
# Reproduce the exact CI environment
docker compose run test-gcc
# Or with specific build type
docker run --rm -e CMAKE_BUILD_TYPE=Debug cler:test
```

#### macOS
```bash
brew install libusb fftw glfw
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

### Interactive Debugging
For difficult issues, trigger a debug session:
1. Go to Actions tab
2. Select workflow and "Run workflow"
3. Check "Enable debug mode with tmate"
4. SSH into the runner when session starts

### Common Issues
- **Docker build fails**: Check Dockerfile syntax and base image availability
- **Missing dependencies**: Update package lists in respective sections
- **Test timeouts**: Increase timeout or optimize test
- **Clang compilation errors**: See KNOWN_ISSUES.md for plots.cpp compilation issue with Clang
- **Windows users**: Use WSL2 to build CLER with full Linux support

## Performance Optimization

### Docker Optimizations
- Multi-stage builds minimize final image size
- Layer caching speeds up rebuilds
- BuildKit features improve build performance

### CI Optimizations
- Parallel job execution across platforms
- Dependency caching reduces install time
- Matrix strategies test multiple configurations efficiently

## Security Considerations

### Docker Security
- Use official base images
- Don't run as root in containers
- Minimize installed packages
- Regular security updates

### Workflow Security
- Pin action versions to commit SHAs
- Use least privilege for GITHUB_TOKEN
- Review third-party actions
- No secrets in Docker images

## Resources

- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Docker Best Practices](https://docs.docker.com/develop/dev-best-practices/)
- [Windows Subsystem for Linux (WSL2)](https://docs.microsoft.com/en-us/windows/wsl/install)
- [Building and Testing C++](https://docs.github.com/en/actions/automating-builds-and-tests/building-and-testing-cpp)