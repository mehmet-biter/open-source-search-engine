#include <iostream>

class Example {
public:
    Example(const std::string &str);
    ~Example();

    void print();
private:
    Example();

    std::string m_str;
};

Example::Example(const std::string &str)
    : m_str(str) {
}

Example::~Example() {
}

void Example::print() {
    std::cout << m_str << std::endl;
}

int main() {
    Example example("my test string");

    example.print();

    return 0;
}