#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <filesystem>
#include <array>
#include <sys/wait.h>
namespace fs = std::filesystem;

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static std::string env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return v ? v : fallback;
}

static const std::string api_key = env_or("OPENROUTER_API_KEY", "");
static const std::string base_url = env_or("OPENROUTER_BASE_URL", "https://openrouter.ai/api/v1");

int execute_read_tool(const json& arguments, std::string& out_result);
int execute_write_tool(const json& arguments, std::string& out_result);
int execute_bash_tool(const json& arguments, std::string& out_result);
static const std::map<std::string, int(*)(const json&, std::string&)> tool_executers = {
    {"Read", execute_read_tool},
    {"Write", execute_write_tool},
    {"Bash", execute_bash_tool},
};

int execute_write_tool(const json& arguments, std::string& out_result){
    if (!arguments.contains("file_path") || !arguments.contains("content")){
        out_result =  "Missing \'file_path\' or \'content\' arguments for Write\n";
        return 1;
    }
    std::string file_path = arguments["file_path"].get<std::string>();
    std::string content = arguments["content"].get<std::string>();
    errno = 0;
    std::ofstream out(file_path);
    if (!out) {
        out_result = "Couldn't open " + file_path + ": " + std::strerror(errno);
        return 1;
    }
    errno = 0;
    out << content;
    out.close(); // flush
    if (!out.good()) {
        out_result = std::string("Write failed: ") + std::strerror(errno);
        return 1;
    }
    out_result = "Wrote successfully";
    return 0;
}

int execute_read_tool(const json& arguments, std::string& out_result){
        if (!arguments.contains("file_path")){
            out_result = "Missing \'file_path\' argument for Read\n";
            return 1;
        }
        std::string file_path = arguments.at("file_path").get<std::string>();
        errno = 0;
        std::ifstream f(file_path);
        if (!f) 
        {
            out_result = "Couldn't open " + file_path + ": " + std::strerror(errno);
            return 1;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        out_result = ss.str();
        return 0;
}

// Returns the command's exit code, or -1 if it couldn't be run at all.
static int run_command(const std::string& cmd, std::string& out_output) {
    errno = 0;
    FILE* pipe = popen((cmd + " < /dev/null 2>&1").c_str(), "r");
    if (!pipe) {
        out_output = std::strerror(errno);
        return -1;
    }
    std::array<char, 4096> buf;
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        out_output.append(buf.data(), n);
    }
    int rc = pclose(pipe);
    if (rc == -1) return -1;
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

int execute_bash_tool(const json& arguments, std::string& out_result){
    if (!arguments.contains("command")){
        out_result = "Missing \'command\' argument for Bash";
        return 1;
    }
    std::string cmd = arguments["command"].get<std::string>();
    int code = run_command(cmd, out_result);
    if (code == -1) return 1;
    if (code != 0) {
        out_result += "\n(exit code " + std::to_string(code) + ")";
    }
    return 0;
}
static void print_indented(const std::string& text, const char* indent = "      ") {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::cerr << indent << line << "\n";
    }
}
static void log_msg(const json& msg){
    std::cerr << msg["role"].get<std::string>() << ":" << std::endl;
    if (msg["content"].is_string()) {
        print_indented(msg["content"].get<std::string>());
    } else {
        print_indented("<Empty message>");
    }
}

int send_request(json& messages, json& tools, json& out_result){
    json request_body = {
        {"model", "anthropic/claude-haiku-4.5"},
        {"messages", messages},
        {"tools", tools},
    };

    cpr::Response response = cpr::Post(
        cpr::Url{base_url + "/chat/completions"},
        cpr::Header{
            {"Authorization", "Bearer " + api_key},
            {"Content-Type", "application/json"}
        },
        cpr::Body{request_body.dump()}
    );

    if (response.status_code != 200) {
        std::cerr << "HTTP error: " << response.status_code << std::endl;
        return 1;
    }

    out_result = json::parse(response.text);
    return 0;

}

int load_tools(json& out_tools) {
    for (const auto& entry : fs::directory_iterator(TOOLS_DIR)) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream f(entry.path());
        if (!f) {std::cerr << "Error loading file " << entry.path()<< std::endl; return 1;}
        out_tools.push_back(json::parse(f));
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 3 || std::string(argv[1]) != "-p") {
        std::cerr << "Expected first argument to be '-p'" << std::endl;
        return 1;
    }

    std::string prompt = argv[2];

    if (prompt.empty()) {
        std::cerr << "Prompt must not be empty" << std::endl;
        return 1;
    }

    if (api_key.empty()) {
        std::cerr << "OPENROUTER_API_KEY is not set" << std::endl;
        return 1;
    }

    json tools = json::array();
    if (load_tools(tools)){
        std::cerr << "loading tools failed" << std::endl;
        return 1;
    }
    
    json messages = json::array({
        {
            {"role", "user"}, 
            {"content", prompt}
        },
    });

    json result;
    if (send_request(messages, tools, result)){
        return 1;
    }

    if (!result.contains("choices") || result["choices"].empty()) {
        std::cerr << "No choices in response" << std::endl;
        return 1;
    }
    
    json msg = result["choices"][0]["message"];
    messages.push_back(msg);
    log_msg(messages.back());
    std::string finish_reason = result["choices"][0]["finish_reason"].get<std::string>();

    while (finish_reason != "stop"){
        if (finish_reason == "tool_calls"){
            for (json& tool : msg["tool_calls"]){
                std::string name = tool["function"]["name"].get<std::string>();
                json arguments = json::parse(tool["function"]["arguments"].get<std::string>());
                std::string tool_result;
                auto it = tool_executers.find(name);
                if (it == tool_executers.end()){  // Tool not found
                    tool_result = "Unknown tool call " + name + "\n";
                }
                else if (it->second(arguments, tool_result)) {
                    tool_result = "Error: tool execution failed\n" + tool_result;
                }
                messages.push_back({
                    {"role", "tool"},
                    {"content", tool_result},
                    {"tool_call_id", tool["id"]},
                });
                log_msg(messages.back());
            }
        }

        if (send_request(messages, tools, result)){
            return 1;
        }
        if (!result.contains("choices") || result["choices"].empty()){
            std::cerr << "No choices in response" << std::endl;
            return 1;
        }
        msg = result["choices"][0]["message"];
        messages.push_back(msg);
        log_msg(messages.back());
        finish_reason = result["choices"][0]["finish_reason"].get<std::string>();
        
    }
    // finish_reason == "stop"
    std::cout << messages.back()["content"].get<std::string>();
    



    
    return 0;
}
