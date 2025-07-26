#include <GLFW/glfw3.h>
#include <iostream>

int main() {
    if (!glfwInit()) {
        std::cout << "GLFW init failed" << std::endl;
        return -1;
    }
    
    std::cout << "GLFW init successful!" << std::endl;
    glfwTerminate();
    return 0;
}