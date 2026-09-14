#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static std::string env_or(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return v ? v : fallback;
}

static const std::string api_key = env_or("OPENROUTER_API_KEY", "");
static const std::string base_url = env_or("OPENROUTER_BASE_URL", "https://openrouter.ai/api/v1");

int execute_read_tool(json& arguments, json& out_result){
        if (!arguments.contains("file_path")){
            std::cerr << "Missing \'file_path\' argument for Read" << std::endl;
            return 1;
        }
        std::ifstream f(arguments["file_path"].get<std::string>());
        if (!f) 
        {
            std::cerr << "File not found " << arguments["file_path"].get<std::string>() << std::endl;
            return 1;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        out_result["role"] = "tool";
        out_result["content"] = ss.str();
        return 0;
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
    for (const auto& name : {"Read"}){
        std::ifstream f(std::string(TOOLS_DIR) + "/" + name + ".json");
        if (!f){
            std::cerr << "Couldn't load tool " << name << std::endl;
            return 1;
        }
        tools.push_back(json::parse(f));
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
    std::string finish_reason = result["choices"][0]["finish_reason"].get<std::string>();

    while (finish_reason != "stop"){
        if (finish_reason == "tool_calls"){
            for (json& tool : msg["tool_calls"]){
                std::string name = tool["function"]["name"].get<std::string>();
                json arguments = json::parse(tool["function"]["arguments"].get<std::string>());
                json tool_result;
                if (name == "Read"){
                    execute_read_tool(arguments, tool_result);
                }
                else if (name == "Write"){
                    continue;
                }
                else{
                    std::cerr << "Unknown tool call " << name << std::endl;
                    return 1;
                }
            }
        }

        if (send_request(messages, tools, result)){
            return 1;
        }
        if (result.contains("choices") || result["choices"].empty()){
            std::cerr << "No choices in response" << std::endl;
            return 1;
        }
        msg = result["choices"][0]["message"];
        messages.push_back(msg);
        finish_reason = result["choices"][0]["finish_reason"].get<std::string>();
        
    }
    // finish_reason == "stop"
    std::cout << messages.back()["contents"].get<std::string>();
    



    
    return 0;
}
