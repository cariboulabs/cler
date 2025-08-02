#include <gtest/gtest.h>
#include <complex>
#include <vector>
#include <cmath>

#include "cler.hpp"
#include "desktop_blocks/math/add.hpp"
#include "desktop_blocks/math/gain.hpp" 
#include "desktop_blocks/math/complex_demux.hpp"

class MathBlocksTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test AddBlock with float inputs
TEST_F(MathBlocksTest, AddBlockFloat) {
    const size_t buffer_size = 1024;
    const size_t num_inputs = 3;
    
    AddBlock<float> add_block("test_add", num_inputs, buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // Fill inputs with test data
    std::vector<float> test_data1 = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    std::vector<float> test_data2 = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f};
    std::vector<float> test_data3 = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
    
    for (size_t i = 0; i < test_data1.size(); i++) {
        add_block.in[0].push(test_data1[i]);
        add_block.in[1].push(test_data2[i]);
        add_block.in[2].push(test_data3[i]);
    }
    
    // Run the block
    auto result = add_block.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify output
    EXPECT_EQ(output.size(), test_data1.size());
    
    for (size_t i = 0; i < test_data1.size(); i++) {
        float expected = test_data1[i] + test_data2[i] + test_data3[i];
        float actual;
        ASSERT_TRUE(output.try_pop(actual));
        EXPECT_FLOAT_EQ(actual, expected) << "Mismatch at index " << i;
    }
}

// Test AddBlock with complex inputs
TEST_F(MathBlocksTest, AddBlockComplex) {
    const size_t buffer_size = 1024;
    const size_t num_inputs = 2;
    
    AddBlock<std::complex<float>> add_block("test_add_complex", num_inputs, buffer_size);
    cler::Channel<std::complex<float>> output(buffer_size);
    
    // Fill inputs with test data
    std::vector<std::complex<float>> test_data1 = {
        {1.0f, 2.0f}, {3.0f, 4.0f}, {5.0f, 6.0f}
    };
    std::vector<std::complex<float>> test_data2 = {
        {0.5f, 0.5f}, {1.5f, 1.5f}, {2.5f, 2.5f}
    };
    
    for (size_t i = 0; i < test_data1.size(); i++) {
        add_block.in[0].push(test_data1[i]);
        add_block.in[1].push(test_data2[i]);
    }
    
    // Run the block
    auto result = add_block.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify output
    EXPECT_EQ(output.size(), test_data1.size());
    
    for (size_t i = 0; i < test_data1.size(); i++) {
        std::complex<float> expected = test_data1[i] + test_data2[i];
        std::complex<float> actual;
        ASSERT_TRUE(output.try_pop(actual));
        EXPECT_FLOAT_EQ(actual.real(), expected.real()) << "Real mismatch at index " << i;
        EXPECT_FLOAT_EQ(actual.imag(), expected.imag()) << "Imag mismatch at index " << i;
    }
}

// Test AddBlock error conditions
TEST_F(MathBlocksTest, AddBlockErrorConditions) {
    // Test minimum inputs requirement
    EXPECT_THROW(AddBlock<float>("test", 1, 1024), std::invalid_argument);
    
    // Test buffer size too small for doubly-mapped buffers (need at least 4096/sizeof(float) = 1024 for float)
    EXPECT_THROW(AddBlock<float>("test", 2, 1), std::invalid_argument);
}

// Test GainBlock with float
TEST_F(MathBlocksTest, GainBlockFloat) {
    const size_t buffer_size = 1024;
    const float gain = 2.5f;
    
    GainBlock<float> gain_block("test_gain", gain, buffer_size);
    cler::Channel<float> output(buffer_size);
    
    // Fill input with test data
    std::vector<float> test_data = {1.0f, -2.0f, 3.5f, -4.2f, 0.0f};
    
    for (float value : test_data) {
        gain_block.in.push(value);
    }
    
    // Run the block
    auto result = gain_block.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify output
    EXPECT_EQ(output.size(), test_data.size());
    
    for (size_t i = 0; i < test_data.size(); i++) {
        float expected = test_data[i] * gain;
        float actual;
        ASSERT_TRUE(output.try_pop(actual));
        EXPECT_FLOAT_EQ(actual, expected) << "Mismatch at index " << i;
    }
}

// Test GainBlock with complex
TEST_F(MathBlocksTest, GainBlockComplex) {
    const size_t buffer_size = 1024;
    const std::complex<float> gain(2.0f, 1.0f);
    
    GainBlock<std::complex<float>> gain_block("test_gain_complex", gain, buffer_size);
    cler::Channel<std::complex<float>> output(buffer_size);
    
    // Fill input with test data
    std::vector<std::complex<float>> test_data = {
        {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {-1.0f, -1.0f}
    };
    
    for (const auto& value : test_data) {
        gain_block.in.push(value);
    }
    
    // Run the block
    auto result = gain_block.procedure(&output);
    EXPECT_TRUE(result.is_ok());
    
    // Verify output
    EXPECT_EQ(output.size(), test_data.size());
    
    for (size_t i = 0; i < test_data.size(); i++) {
        std::complex<float> expected = test_data[i] * gain;
        std::complex<float> actual;
        ASSERT_TRUE(output.try_pop(actual));
        EXPECT_FLOAT_EQ(actual.real(), expected.real()) << "Real mismatch at index " << i;
        EXPECT_FLOAT_EQ(actual.imag(), expected.imag()) << "Imag mismatch at index " << i;
    }
}

// Test ComplexToMagPhaseBlock in MagPhase mode
TEST_F(MathBlocksTest, ComplexDemuxMagPhase) {
    const size_t buffer_size = 1024;
    
    ComplexToMagPhaseBlock demux_block("test_demux", ComplexToMagPhaseBlock::Mode::MagPhase, buffer_size);
    cler::Channel<float> magnitude_out(buffer_size);
    cler::Channel<float> phase_out(buffer_size);
    
    // Fill input with test data
    std::vector<std::complex<float>> test_data = {
        {1.0f, 0.0f},     // magnitude=1, phase=0
        {0.0f, 1.0f},     // magnitude=1, phase=π/2
        {-1.0f, 0.0f},    // magnitude=1, phase=π
        {0.0f, -1.0f},    // magnitude=1, phase=-π/2
        {3.0f, 4.0f}      // magnitude=5, phase=atan2(4,3)
    };
    
    for (const auto& value : test_data) {
        demux_block.in.push(value);
    }
    
    // Run the block
    auto result = demux_block.procedure(&magnitude_out, &phase_out);
    EXPECT_TRUE(result.is_ok());
    
    // Verify output
    EXPECT_EQ(magnitude_out.size(), test_data.size());
    EXPECT_EQ(phase_out.size(), test_data.size());
    
    for (size_t i = 0; i < test_data.size(); i++) {
        float expected_mag = std::abs(test_data[i]);
        float expected_phase = std::arg(test_data[i]);
        
        float actual_mag, actual_phase;
        ASSERT_TRUE(magnitude_out.try_pop(actual_mag));
        ASSERT_TRUE(phase_out.try_pop(actual_phase));
        
        EXPECT_FLOAT_EQ(actual_mag, expected_mag) << "Magnitude mismatch at index " << i;
        EXPECT_FLOAT_EQ(actual_phase, expected_phase) << "Phase mismatch at index " << i;
    }
}

// Test ComplexToMagPhaseBlock in RealImag mode
TEST_F(MathBlocksTest, ComplexDemuxRealImag) {
    const size_t buffer_size = 1024;
    
    ComplexToMagPhaseBlock demux_block("test_demux", ComplexToMagPhaseBlock::Mode::RealImag, buffer_size);
    cler::Channel<float> real_out(buffer_size);
    cler::Channel<float> imag_out(buffer_size);
    
    // Fill input with test data
    std::vector<std::complex<float>> test_data = {
        {1.5f, 2.5f}, {-3.0f, 4.0f}, {0.0f, -1.0f}, {7.2f, 0.0f}
    };
    
    for (const auto& value : test_data) {
        demux_block.in.push(value);
    }
    
    // Run the block
    auto result = demux_block.procedure(&real_out, &imag_out);
    EXPECT_TRUE(result.is_ok());
    
    // Verify output
    EXPECT_EQ(real_out.size(), test_data.size());
    EXPECT_EQ(imag_out.size(), test_data.size());
    
    for (size_t i = 0; i < test_data.size(); i++) {
        float expected_real = test_data[i].real();
        float expected_imag = test_data[i].imag();
        
        float actual_real, actual_imag;
        ASSERT_TRUE(real_out.try_pop(actual_real));
        ASSERT_TRUE(imag_out.try_pop(actual_imag));
        
        EXPECT_FLOAT_EQ(actual_real, expected_real) << "Real mismatch at index " << i;
        EXPECT_FLOAT_EQ(actual_imag, expected_imag) << "Imag mismatch at index " << i;
    }
}

// Test ComplexToMagPhaseBlock error conditions
TEST_F(MathBlocksTest, ComplexDemuxErrorConditions) {
    // Test buffer size too small for doubly-mapped buffers (need at least 4096/sizeof(complex<float>) = 512 for complex<float>)
    EXPECT_THROW(ComplexToMagPhaseBlock("test", ComplexToMagPhaseBlock::Mode::MagPhase, 1), std::invalid_argument);
    
    // Test invalid mode (this test might not work with current enum, but shows the pattern)
    // EXPECT_THROW(ComplexToMagPhaseBlock("test", static_cast<ComplexToMagPhaseBlock::Mode>(999), 1024), std::invalid_argument);
}

// Test empty input handling for all blocks
TEST_F(MathBlocksTest, EmptyInputHandling) {
    const size_t buffer_size = 1024;
    
    // Test AddBlock with empty inputs
    {
        AddBlock<float> add_block("test_add_empty", 2, buffer_size);
        cler::Channel<float> output(buffer_size);
        
        auto result = add_block.procedure(&output);
        EXPECT_TRUE(result.is_ok());
        EXPECT_EQ(output.size(), 0);
    }
    
    // Test GainBlock with empty input
    {
        GainBlock<float> gain_block("test_gain_empty", 2.0f, buffer_size);
        cler::Channel<float> output(buffer_size);
        
        auto result = gain_block.procedure(&output);
        EXPECT_TRUE(result.is_ok());
        EXPECT_EQ(output.size(), 0);
    }
    
    // Test ComplexDemux with empty input
    {
        ComplexToMagPhaseBlock demux_block("test_demux_empty", ComplexToMagPhaseBlock::Mode::MagPhase, buffer_size);
        cler::Channel<float> out1(buffer_size);
        cler::Channel<float> out2(buffer_size);
        
        auto result = demux_block.procedure(&out1, &out2);
        EXPECT_TRUE(result.is_ok());
        EXPECT_EQ(out1.size(), 0);
        EXPECT_EQ(out2.size(), 0);
    }
}