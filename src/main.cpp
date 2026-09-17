#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <filesystem>
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

int execute_write_tool(const json& arguments, std::string& out);
int execute_read_tool(const json& arguments, std::string& out);
static const std::map<std::string, int(*)(const json&, std::string&)> tool_executers = {
    {"Read", execute_read_tool},
    {"Write", execute_write_tool},
};

int execute_write_tool(const json& arguments, std::string& out){
    std::cerr << "Not Implemented" << std::endl;
    return 1;
}
int execute_read_tool(const json& arguments, std::string& out){
        if (!arguments.contains("file_path")){
            std::cerr << "Missing \'file_path\' argument for Read" << std::endl;
            return 1;
        }
        std::ifstream f(arguments.at("file_path").get<std::string>());
        if (!f) 
        {
            std::cerr << "File not found " << arguments["file_path"].get<std::string>() << std::endl;
            return 1;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return 0;
}

void log_msg(const json& msg){
    std::cerr << msg["role"].get<std::string>() << ":" << std::endl;
    if (msg["content"].is_string()) {
        std::cerr << "      " << msg["content"].get<std::string>() << std::endl;
    }
    else{
        std::cerr << "      " << "<Empty message>" << std::endl;
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
                if (it == tool_executers.end()){
                    std::cerr << "Unknown tool call " << name << std::endl;
                    return 1;
                }
                if (it->second(arguments, tool_result)) {
                    tool_result = "Error: tool execution failed";
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
