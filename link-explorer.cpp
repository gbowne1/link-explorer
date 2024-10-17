#include <iostream>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <fstream>
#include <regex>
#include <sstream>
#include <set>
#include <algorithm>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <queue>

// Function prototypes
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
void crawl(const std::string& url, int maxThreads, int delay, int depth);
std::vector<std::string> extractLinks(const std::string& html);
void displayLinks(const std::vector<std::string>& links);
void saveLinks(const std::vector<std::string>& links);
std::set<std::string> generatePermutations(const std::string& baseUrl);
bool isValidURL(const std::string& url);
bool isAllowedByRobots(const std::string& url);
bool checkLink(const std::string& url);
void worker(const std::string& targetUrl, std::vector<std::string>& foundLinks, std::mutex& mtx, int delay);

std::atomic<int> activeThreads(0);

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <url> <maxThreads> <delay> <depth>" << std::endl;
        return 1;
    }

    std::string url(argv[1]);
    int maxThreads = std::stoi(argv[2]);
    int delay = std::stoi(argv[3]);
    int depth = std::stoi(argv[4]);

    if (!isValidURL(url)) {
        std::cerr << "Invalid URL." << std::endl;
        return 1;
    }

    if (!isAllowedByRobots(url)) {
        std::cerr << "Crawling is disallowed by robots.txt." << std::endl;
        return 1;
    }

    crawl(url, maxThreads, delay, depth);

    return 0;
}

// Callback function for CURL
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Check if crawling is allowed by robots.txt
bool isAllowedByRobots(const std::string& url) {
    std::string robotsUrl = url + "/robots.txt";
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, robotsUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }

    return readBuffer.find("Disallow: /") == std::string::npos;
}

// Crawl the provided URL and its permutations
void crawl(const std::string& url, int maxThreads, int delay, int depth) {
    std::set<std::string> urlsToVisit = generatePermutations(url);
    std::vector<std::string> foundLinks;
    std::vector<std::thread> threadPool;
    std::mutex mtx;

    for (const auto& targetUrl : urlsToVisit) {
        if (activeThreads >= maxThreads) {
            std::this_thread::yield(); // Wait until a thread is available
        }

        threadPool.emplace_back(worker, targetUrl, std::ref(foundLinks), std::ref(mtx), delay);
    }

    for (auto& thread : threadPool) {
        thread.join();
    }

    displayLinks(foundLinks);

    char saveChoice;
    std::cout << "Do you want to save the links to a file? (y/n): ";
    std::cin >> saveChoice;
    if (saveChoice == 'Y' || saveChoice == 'y') {
        saveLinks(foundLinks);
    }
}

// Worker function for crawling
void worker(const std::string& targetUrl, std::vector<std::string>& foundLinks, std::mutex& mtx, int delay) {
    activeThreads++;
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, targetUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res == CURLE_OK) {
            auto links = extractLinks(readBuffer);
            std::lock_guard<std::mutex> lock(mtx);
            for (const auto& link : links) {
                if (checkLink(link)) {
                    foundLinks.push_back(link);
                }
            }
        } else {
            std::cerr << "Error fetching URL: " << targetUrl << " - " << curl_easy_strerror(res) << std::endl;
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(delay)); // Delay between requests
    activeThreads--;
}

// Extract links from the HTML content
std::vector<std::string> extractLinks(const std::string& html) {
    std::vector<std::string> links;
    std::regex linkRegex(R"((http|https)://[^\s"]+)");
    std::smatch match;
    std::string::const_iterator searchStart(html.cbegin());

    while (std::regex_search(searchStart, html.cend(), match, linkRegex)) {
        links.push_back(match[0]);
        searchStart = match.suffix().first;
    }

    return links;
}

// Check if a link is reachable
bool checkLink(const std::string& url) {
    CURL* curl;
    CURLcode res;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // Only check headers
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }

    return (res == CURLE_OK);
}

// Display the links in a formatted way
void displayLinks(const std::vector<std::string>& links) {
    std::cout << "┌────────────────────────────────────┐" << std::endl;
    std::cout << "│           Found Links              │" << std::endl;
    std::cout << "├────────────────────────────────────┤" << std::endl;
    for (const auto& link : links) {
        std::cout << "│ " << link << std::endl;
    }
    std::cout << "└────────────────────────────────────┘" << std::endl;
}

// Save the links to a file
void saveLinks(const std::vector<std::string>& links) {
    std::ofstream outFile("links.txt");
    if (outFile.is_open()) {
        for (const auto& link : links) {
            outFile << link << std::endl;
        }
        outFile.close();
        std::cout << "Links saved to links.txt" << std::endl;
    } else {
        std::cerr << "Error opening file for writing." << std::endl;
    }
}

// Generate permutations for the base URL
std::set<std::string> generatePermutations(const std::string& baseUrl) {
    std::set<std::string> urls;
    urls.insert("http://" + baseUrl);
    urls.insert("https://" + baseUrl);
    urls.insert("http://www." + baseUrl);
    urls.insert("https://www." + baseUrl);

    std::vector<std::string> subdomains = {"", "www", "blog", "shop", "test"};
    for (const auto& sub : subdomains) {
        if (!sub.empty()) {
            urls.insert("http://" + sub + "." + baseUrl);
            urls.insert("https://" + sub + "." + baseUrl);
        }
    }

    return urls;
}

// Check if a URL is valid
bool isValidURL(const std::string& url) {
    std::regex pattern(R"(^((http|https)://)?([a-zA-Z0-9\-]+\.[a-zA-Z]{2,6}|localhost)(:\d+)?(/[\w\-._~:/?#[\]@!$&'()*+,;=%]*)?$)");
    return std::regex_match(url, pattern);
}
