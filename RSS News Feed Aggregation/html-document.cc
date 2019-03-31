/**
 * File: html-document.cc
 * ----------------------
 * Presents the implementation for the HTMLDocument class,
 * which relies on libxml2's ability to parse a single document 
 * and extract the textual content from the body tag.
 */

#include <iostream>
#include <vector>
#include <cassert>
#include <sstream>

#define BOOST_NETWORK_ENABLE_HTTPS
#include <boost/network/protocol/http/client.hpp>
#include <boost/network/uri.hpp>
#include <myhtml/api.h>

#include "html-document.h"
#include "html-document-exception.h"
#include "stream-tokenizer.h"

using namespace std;
using namespace boost::network;
using namespace boost::network::http;

void HTMLDocument::parse() throw (HTMLDocumentException) {
  	parse(download());
}

std::string HTMLDocument::download(size_t numRedirectsAllowed) throw (HTMLDocumentException)  {
	std::string url = this->url;
	for (size_t i = 0; i < numRedirectsAllowed; i++) {
		try {
    		client::request req(url);
    		client::options opts;
    		opts.always_verify_peer(true).
			    openssl_sni_hostname(req.host()).
			    timeout(20);
    		client cl(opts);
    		client::response resp = cl.get(req);
			auto head = headers(resp);
			if (head.count("Location") == 0 && head.count("location") == 0) return body(resp);
			const char *key = head.count("Location") > 0 ? "Location" : "location";
			url = head[key].begin()->second;
		} catch (exception& e) {
			throw HTMLDocumentException("Error downloading document from " + this->url + ":\n" + e.what());
		}
	}
	throw HTMLDocumentException("Error downloading document from " + this->url + ":\nToo many redirects.");
}

void HTMLDocument::parse(const std::string& contents) throw (HTMLDocumentException)  {
	myhtml_t* myhtml = myhtml_create();
	if (myhtml == NULL) throw runtime_error("Failed to alloc MyHTML parser");
	if (myhtml_init(myhtml, MyHTML_OPTIONS_DEFAULT, 1, 0) != MyHTML_STATUS_OK) {
		throw runtime_error("Failed to initialize MyHTML parser");
	}
	myhtml_tree_t *tree = myhtml_tree_create();
	if (tree == NULL) throw runtime_error("Failed to alloc MyHTML tree");
	if (myhtml_tree_init(tree, myhtml) != MyHTML_STATUS_OK) {
		throw runtime_error("Failed to initialize MyHTML tree");
	}
	if (myhtml_parse(tree, MyENCODING_UTF_8, contents.c_str(), contents.length()) != MyHTML_STATUS_OK) {
		throw HTMLDocumentException("Failed to parse document body as HTML!");
	}
	extractTokens(tree);                                                                                                                                                                           
	if (myhtml_tree_destroy(tree) != NULL) {
		throw runtime_error("MyHTML failed to free parse tree");
	}
	if (myhtml_destroy(myhtml) != NULL) {
		throw runtime_error("MyHTML failed to free myhtml object");
	}
}

static const std::string kDelimiters = " \t\n\r\b!@#$%^&*()_-+=~`{[}]|\\\"':;<,>.?/";
static const std::string kSeparator = " ";
void HTMLDocument::extractTokens(myhtml_tree_t *tree) throw (HTMLDocumentException) {
	removeNodes(tree, "style");
	removeNodes(tree, "script");
	myhtml_tree_node_t *body = myhtml_tree_get_node_body(tree);
	if (body == NULL) {
		throw runtime_error("MyHTML failed to find the body of the overall HTML tree.");
	}
	myhtml_collection_t *nodes = myhtml_get_nodes_by_tag_id_in_scope(tree, NULL, body, MyHTML_TAG__TEXT, NULL);
	if (nodes == NULL) {
		throw runtime_error("MyHTML failed to allocate list of text nodes");
	}
	
	std::string serialization;
	for (size_t i = 0; i < nodes->length; i++)  {
		myhtml_tree_node_t *node = nodes->list[i];
		assert(myhtml_node_tag_id(node) == MyHTML_TAG__TEXT);
		serialization += myhtml_node_text(node, NULL) + kSeparator;
		std::istringstream instream(myhtml_node_text(node, NULL));
		StreamTokenizer st(instream, kDelimiters);
		while (st.hasMoreTokens()) {
			tokens.push_back(st.nextToken());
		}
		serialization += myhtml_node_text(node, NULL) + kSeparator;
	}
	std::istringstream instream(serialization);
	StreamTokenizer st(instream, kDelimiters);
	while (st.hasMoreTokens()) {
		tokens.push_back(st.nextToken());
	}

	if (myhtml_collection_destroy(nodes) != NULL) {
		throw runtime_error("MyHTML failed to free resources for list of text nodes");
	}
}

void HTMLDocument::removeNodes(myhtml_tree_t *tree, const std::string& tagName) throw (HTMLDocumentException) {
	myhtml_collection_t *nodes = myhtml_get_nodes_by_name(tree, NULL, tagName.c_str(), tagName.size(), NULL);
	if (nodes == NULL) {
		throw runtime_error("MyHTML failed to allocate list of text nodes");
	}
	
	for (size_t i = 0; i < nodes->length; i++)  {
		myhtml_tree_node_t *node = nodes->list[i];
		myhtml_node_delete_recursive(node);
	}
	
	if (myhtml_collection_destroy(nodes) != NULL) {
		throw runtime_error("MyHTML failed to free resources for list of text nodes");
	}
}
