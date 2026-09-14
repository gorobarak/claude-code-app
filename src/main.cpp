#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

    const char* api_key_env = std::getenv("OPENROUTER_API_KEY");
    const char* base_url_env = std::getenv("OPENROUTER_BASE_URL");

    std::string api_key = api_key_env ? api_key_env : "";
    std::string base_url = base_url_env ? base_url_env : "https://openrouter.ai/api/v1";

    if (api_key.empty()) {
        std::cerr << "OPENROUTER_API_KEY is not set" << std::endl;
        return 1;
    }
    json tools = json::array();
    for (const auto& name : {"Read"}){
        std::ifstream f(std::string(TOOLS_DIR) + "/" + name + ".json");
        if (!f){
            std::cerr << "Cooludn;t load tool " << name << std::endl;
            return 1;
        }
        tools.push_back(json::parse(f));
    } 
    json request_body = {
        {"model", "anthropic/claude-haiku-4.5"},
        {"messages", json::array({
            {{"role", "user"}, {"content", prompt}}
        })},
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

    json result = json::parse(response.text);

    if (!result.contains("choices") || result["choices"].empty()) {
        std::cerr << "No choices in response" << std::endl;
        return 1;
    }
    json msg = result["choices"][0]["message"];
    std::string finish_reason = result["choices"][0]["finish_reason"].get<std::string>();
    if (finish_reason == "tool_calls"){
        json tool = msg["tool_calls"][0];
        std::string name = tool["function"]["name"].get<std::string>();
        if (name == "Read"){
            json arguments = json::parse(tool["function"]["arguments"].get<std::string>());
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
            std::cout << ss.str();
        }
        
        
    }
    else if (finish_reason == "stop")
    {
        std::cout << msg["content"].get<std::string>();
    }
    



    
    return 0;
}
