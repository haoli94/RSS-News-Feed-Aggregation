/**
 * File: news-aggregator.cc
 * --------------------------------
 * Presents the implementation of the NewsAggregator class.
 */

#include "news-aggregator.h"
#include <iostream>
#include <iomanip>
#include <getopt.h>
#include <libxml/parser.h>
#include <libxml/catalog.h>
// you will almost certainly need to add more system header includes

// I'm not giving away too much detail here by leaking the #includes below,
// which contribute to the official CS110 staff solution.
#include <vector>
#include <mutex>
#include <thread>
#include <map>
#include "semaphore.h"
#include <unordered_map>
#include <unordered_set>
#include <utility> 

#include "rss-feed.h"
#include "rss-feed-list.h"
#include "html-document.h"
#include "html-document-exception.h"
#include "rss-feed-exception.h"
#include "rss-feed-list-exception.h"
#include "utils.h"
#include "ostreamlock.h"
#include "string-utils.h"
using namespace std;

/**
 * Factory Method: createNewsAggregator
 * ------------------------------------
 * Factory method that spends most of its energy parsing the argument vector
 * to decide what rss feed list to process and whether to print lots of
 * of logging information as it does so.
 */
static const string kDefaultRSSFeedListURL = "small-feed.xml";
NewsAggregator *NewsAggregator::createNewsAggregator(int argc, char *argv[]) {
    struct option options[] = {
	{"verbose", no_argument, NULL, 'v'},
	{"quiet", no_argument, NULL, 'q'},
	{"url", required_argument, NULL, 'u'},
	{NULL, 0, NULL, 0},
    };

    string rssFeedListURI = kDefaultRSSFeedListURL;
    bool verbose = false;
    while (true) {
	int ch = getopt_long(argc, argv, "vqu:", options, NULL);
	if (ch == -1) break;
	switch (ch) {
	    case 'v':
		verbose = true;
		break;
	    case 'q':
		verbose = false;
		break;
	    case 'u':
		rssFeedListURI = optarg;
		break;
	    default:
		NewsAggregatorLog::printUsage("Unrecognized flag.", argv[0]);
	}
    }

    argc -= optind;
    if (argc > 0) NewsAggregatorLog::printUsage("Too many arguments.", argv[0]);
    return new NewsAggregator(rssFeedListURI, verbose);
}

/**
 * Method: buildIndex
 * ------------------
 * Initalizex the XML parser, processes all feeds, and then
 * cleans up the parser.  The lion's share of the work is passed
 * on to processAllFeeds, which you will need to implement.
 */
void NewsAggregator::buildIndex() {
    if (built) return;
    built = true; // optimistically assume it'll all work out
    xmlInitParser();
    xmlInitializeCatalog();
    processAllFeeds();
    xmlCatalogCleanup();
    xmlCleanupParser();
}

/**
 * Method: queryIndex
 * ------------------
 * Interacts with the user via a custom command line, allowing
 * the user to surface all of the news articles that contains a particular
 * search term.
 */
void NewsAggregator::queryIndex() const {
    static const size_t kMaxMatchesToShow = 15;
    while (true) {
	cout << "Enter a search term [or just hit <enter> to quit]: ";
	string response;
	getline(cin, response);
	response = trim(response);
	if (response.empty()) break;
	const vector<pair<Article, int> >& matches = index.getMatchingArticles(response);
	if (matches.empty()) {
	    cout << "Ah, we didn't find the term \"" << response << "\". Try again." << endl;
	} else {
	    cout << "That term appears in " << matches.size() << " article"
		<< (matches.size() == 1 ? "" : "s") << ".  ";
	    if (matches.size() > kMaxMatchesToShow)
		cout << "Here are the top " << kMaxMatchesToShow << " of them:" << endl;
	    else if (matches.size() > 1)
		cout << "Here they are:" << endl;
	    else
		cout << "Here it is:" << endl;
	    size_t count = 0;
	    for (const pair<Article, int>& match: matches) {
		if (count == kMaxMatchesToShow) break;
		count++;
		string title = match.first.title;
		if (shouldTruncate(title)) title = truncate(title);
		string url = match.first.url;
		if (shouldTruncate(url)) url = truncate(url);
		string times = match.second == 1 ? "time" : "times";
		cout << "  " << setw(2) << setfill(' ') << count << ".) "
		    << "\"" << title << "\" [appears " << match.second << " " << times << "]." << endl;
		cout << "       \"" << url << "\"" << endl;
	    }
	}
    }
}


/**
 * Private Constructor: NewsAggregator
 * -----------------------------------
 * Self-explanatory.  You may need to add a few lines of code to
 * initialize any additional fields you add to the private section
 * of the class definition.
 */
NewsAggregator::NewsAggregator(const string& rssFeedListURI, bool verbose): 
    log(verbose), rssFeedListURI(rssFeedListURI), built(false),
    feedPool(kNumMaxFeed), articlePool(kNumMaxArticle) {}

/**
 * Private Method: processAllFeeds
 * -------------------------------
 * Downloads and parses the encapsulated RSSFeedList, which itself
 * leads to RSSFeeds, which themsleves lead to HTMLDocuemnts, which
 * can be collectively parsed for their tokens to build a huge RSSIndex.
 * 
 * The vast majority of your Assignment 5 work has you implement this
 * method using multithreading while respecting the imposed constraints
 * outlined in the spec.
 */


void NewsAggregator::articleThreads(std::vector<Article> articles){
	//std::mutex articleMapLock;
	for(auto iter = articles.begin(); iter != articles.end(); iter++) {
		articlePool.schedule([this, iter]{
        	Article article = *iter;
			urlSetLock.lock();
            if(urlSet.count(article.url)) {
           		urlSetLock.unlock();
                return;
            } else {
            	urlSet.insert(article.url);
                urlSetLock.unlock();
            }
			server Server = getURLServer(article.url);
			HTMLDocument htmlDocument(article.url);
			try{
                htmlDocument.parse();
            } catch(const HTMLDocumentException& hde) {
                return;
            }
			const auto& const_tokens = htmlDocument.getTokens();
            vector<std::string> tokens(const_tokens.begin(), const_tokens.end());
            sort(tokens.begin(), tokens.end());
            //assert(is_sorted(tokens.cbegin(), tokens.cend()));
            Article newArticle;
			const auto& serverIt = ArticleMap.find(Server);
            if(serverIt != ArticleMap.end() &&
            serverIt->second.find(article.title) != serverIt->second.end()) {
				vector<string> newTokens;
                Article old = ArticleMap[Server][article.title].first;
                vector<string> oldTokens = ArticleMap[Server][article.title].second;
                sort(oldTokens.begin(), oldTokens.end());
                set_intersection(oldTokens.cbegin(), oldTokens.cend(),
                                        tokens.cbegin(), tokens.cend(), back_inserter(newTokens));

                newArticle = min(old,article);
                ArticleMap[Server][article.title] = make_pair(newArticle, newTokens);
                ArticleMapLock.unlock();
	    }else {
                ArticleMap[Server][article.title] = make_pair(article, tokens);
                ArticleMapLock.unlock();	
		//cout << article.title << endl;
            }
		
        });
    }
    articlePool.wait();
}



void NewsAggregator::feedThread(const pair<string, string>& feed) {

    urlSetLock.lock();
    std::string xmlUrl = feed.first;
    std::string xmlTitle = feed.second;
    if(urlSet.count(xmlUrl)) {
        urlSetLock.unlock();
        return;
    }
    else {
        urlSet.insert(xmlUrl);
        urlSetLock.unlock();
    }
    RSSFeed rssFeed(xmlUrl);
    try{
        rssFeed.parse();
    } catch(const RSSFeedException& exception) {
        log.noteSingleFeedDownloadFailure(xmlUrl);
        return;
    }
    const auto& articles = rssFeed.getArticles(); 
    articleThreads(articles);
}



void NewsAggregator::processAllFeeds() {

    RSSFeedList rssFeedList(rssFeedListURI);
    //* Usage: RSSFeedList rssFeedList("large-feed.xml");
    try {
	rssFeedList.parse();
    }catch(const RSSFeedListException& rfle) {
	log.noteFullRSSFeedListDownloadFailureAndExit(rssFeedListURI);
	return;
    }
    const auto& feeds = rssFeedList.getFeeds();
    //* Usage: const auto& feeds = list.getFeeds();
    for (const pair<string, string>& feed : feeds) {
	feedPool.schedule([this,feed] {
		feedThread(feed);
	});
    }
    feedPool.wait();
    for(auto serverIt = ArticleMap.begin(); serverIt != ArticleMap.end(); serverIt++) {
	for (auto articleIt = serverIt->second.cbegin(); articleIt != serverIt->second.cend(); articleIt++) {
	    indexSetLock.lock();
	    index.add(articleIt->second.first, articleIt->second.second);
	    indexSetLock.unlock();
	}
    }




}
