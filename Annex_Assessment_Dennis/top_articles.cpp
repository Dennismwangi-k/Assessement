#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace {

struct Article {
    string name;
    long long comments;
};

FILE* openPipe(const string& command, const char* mode) {
#ifdef _WIN32
    return _popen(command.c_str(), mode);
#else
    return popen(command.c_str(), mode);
#endif
}

int closePipe(FILE* pipe) {
#ifdef _WIN32
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}

string fetchPage(int page) {
    string command =
        "curl -s --fail \"https://jsonmock.hackerrank.com/api/articles?page=" +
        to_string(page) + "\"";

    FILE* pipe = openPipe(command, "r");
    if (pipe == nullptr) {
        throw runtime_error("Failed to start curl process.");
    }

    string response;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        response += buffer;
    }

    int exit_code = closePipe(pipe);
    if (exit_code != 0 || response.empty()) {
        throw runtime_error("Failed to fetch API response.");
    }

    return response;
}

size_t skipWhitespace(const string& text, size_t index) {
    while (index < text.size() &&
           isspace(static_cast<unsigned char>(text[index]))) {
        index++;
    }
    return index;
}

size_t findFieldValueStart(const string& object, const string& key) {
    string token = "\"" + key + "\"";
    size_t key_pos = object.find(token);
    if (key_pos == string::npos) {
        return string::npos;
    }

    size_t colon_pos = object.find(':', key_pos + token.size());
    if (colon_pos == string::npos) {
        return string::npos;
    }

    return skipWhitespace(object, colon_pos + 1);
}

void appendUtf8(string& output, unsigned code_point) {
    if (code_point <= 0x7F) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

unsigned hexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return static_cast<unsigned>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return static_cast<unsigned>(10 + ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return static_cast<unsigned>(10 + ch - 'A');
    }
    throw runtime_error("Invalid unicode escape sequence.");
}

bool parseJsonString(const string& text, size_t start, string& value) {
    if (start == string::npos || start >= text.size() || text[start] != '"') {
        return false;
    }

    value.clear();
    size_t index = start + 1;
    while (index < text.size()) {
        char ch = text[index++];
        if (ch == '"') {
            return true;
        }

        if (ch != '\\') {
            value.push_back(ch);
            continue;
        }

        if (index >= text.size()) {
            throw runtime_error("Incomplete escape sequence in JSON string.");
        }

        char escaped = text[index++];
        switch (escaped) {
            case '"':
            case '\\':
            case '/':
                value.push_back(escaped);
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case 'u': {
                if (index + 4 > text.size()) {
                    throw runtime_error("Truncated unicode escape in JSON string.");
                }

                unsigned code_point = 0;
                for (int i = 0; i < 4; i++) {
                    code_point = (code_point << 4) | hexValue(text[index++]);
                }
                appendUtf8(value, code_point);
                break;
            }
            default:
                throw runtime_error("Unsupported escape sequence in JSON string.");
        }
    }

    throw runtime_error("Unterminated JSON string.");
}

bool parseOptionalStringField(
    const string& object,
    const string& key,
    string& value
) {
    size_t value_start = findFieldValueStart(object, key);
    if (value_start == string::npos) {
        return false;
    }

    if (object.compare(value_start, 4, "null") == 0) {
        return false;
    }

    return parseJsonString(object, value_start, value);
}

long long parseOptionalIntegerField(
    const string& object,
    const string& key,
    long long default_value
) {
    size_t value_start = findFieldValueStart(object, key);
    if (value_start == string::npos || object.compare(value_start, 4, "null") == 0) {
        return default_value;
    }

    size_t value_end = value_start;
    if (object[value_end] == '-') {
        value_end++;
    }
    while (value_end < object.size() &&
           isdigit(static_cast<unsigned char>(object[value_end]))) {
        value_end++;
    }

    return stoll(object.substr(value_start, value_end - value_start));
}

int parseTotalPages(const string& response) {
    return static_cast<int>(
        parseOptionalIntegerField(response, "total_pages", 0)
    );
}

vector<string> extractObjectsFromDataArray(const string& response) {
    vector<string> objects;
    size_t data_key_pos = response.find("\"data\"");
    if (data_key_pos == string::npos) {
        return objects;
    }

    size_t array_start = response.find('[', data_key_pos);
    if (array_start == string::npos) {
        return objects;
    }

    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    size_t object_start = string::npos;

    for (size_t index = array_start + 1; index < response.size(); index++) {
        char ch = response[index];

        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }

        if (ch == '{') {
            if (depth == 0) {
                object_start = index;
            }
            depth++;
        } else if (ch == '}') {
            depth--;
            if (depth == 0 && object_start != string::npos) {
                objects.push_back(response.substr(object_start, index - object_start + 1));
                object_start = string::npos;
            }
        } else if (ch == ']' && depth == 0) {
            break;
        }
    }

    return objects;
}

vector<Article> parseArticles(const string& response) {
    vector<Article> articles;
    vector<string> objects = extractObjectsFromDataArray(response);

    for (const string& object : objects) {
        string article_name;
        if (!parseOptionalStringField(object, "title", article_name)) {
            if (!parseOptionalStringField(object, "story_title", article_name)) {
                continue;
            }
        }

        long long comments = parseOptionalIntegerField(object, "num_comments", 0);
        articles.push_back({article_name, comments});
    }

    return articles;
}

}  // namespace

vector<string> topArticles(int limit) {
    vector<Article> articles;
    string first_page = fetchPage(1);
    int total_pages = parseTotalPages(first_page);
    vector<Article> first_page_articles = parseArticles(first_page);
    articles.insert(
        articles.end(),
        first_page_articles.begin(),
        first_page_articles.end()
    );

    for (int page = 2; page <= total_pages; page++) {
        string response = fetchPage(page);
        vector<Article> page_articles = parseArticles(response);
        articles.insert(articles.end(), page_articles.begin(), page_articles.end());
    }

    sort(articles.begin(), articles.end(), [](const Article& left, const Article& right) {
        if (left.comments != right.comments) {
            return left.comments > right.comments;
        }
        return left.name > right.name;
    });

    vector<string> ranked_names;
    int result_count = min(limit, static_cast<int>(articles.size()));
    ranked_names.reserve(result_count);

    for (int index = 0; index < result_count; index++) {
        ranked_names.push_back(articles[index].name);
    }

    return ranked_names;
}

int main() {
    int limit;
    if (!(cin >> limit)) {
        return 1;
    }

    vector<string> articles = topArticles(limit);
    for (const string& article : articles) {
        cout << article << '\n';
    }

    return 0;
}
