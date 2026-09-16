#include <iostream>
#include <string>
#include <optional> 
#include <vector>
#include <filesystem>
#include <fstream>
namespace fs = std::filesystem;

struct Commit{
    std::string id;
    std::string parent;
    std::string message;
};

bool isValidField(const std::string& text) {
    return text.find('|') == std::string::npos &&
           text.find('\n') == std::string::npos &&
           text.find('\r') == std::string::npos;
}

bool saveLog(const fs::path& log_path, const std::vector<Commit>& commits) {
    std::error_code ec;
    fs::create_directory(log_path.parent_path(), ec);
    if (ec) {
        std::cerr << "Error creating directory: " << ec.message() << std::endl;
        return false;
    }
    std::ofstream output(log_path);
    if (!output) {
        std::cerr << "Error opening file: " << log_path << std::endl;
        return false;
    }

    for (const auto& commit : commits) {
        const auto serialized = serializeCommit(commit);
        if (serialized) {
            output << *serialized << std::endl;
        }
    }


    return true;
}


std::optional<std::string> serializeCommit(const Commit& commit) {
    if(!isValidField(commit.id) || !isValidField(commit.parent) || !isValidField(commit.message)) {
        return std::nullopt; 
    }
    return commit.id + "|" + commit.parent + "|" + commit.message;
}

int main() {
    //输出我的信息  我叫 myx 22岁
    std::cout << "myx" << std::endl;
    std::cout << "22" << std::endl;
    std::cout << "Testing commit serialization..." << std::endl;
    std::cout << "Testing commit serialization..." << std::endl;
    const Commit good{"c002", "c001", "Initial commit"};
    const Commit bad{"c003", "c002", "This commit has a | in the message"};
    for (const auto& c : {good, bad}) {
        const auto serialized = serializeCommit(c);
        if (serialized) {
            std::cout << "Serialized commit: " << *serialized << std::endl;
        } else {
            std::cout << "Failed to serialize commit with id: " << c.id << std::endl;
        }
    }
}