#include "query_processor.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>

namespace {

// ------------------------------------------------------------- expression AST

struct Node {
    enum class Type { Term, And, Or, Not };

    explicit Node(Type node_type, std::string node_term = {})
        : type(node_type), term(std::move(node_term)) {}

    Type type;
    std::string term;            // Term only
    std::unique_ptr<Node> left;  // And, Or, Not (the negated child)
    std::unique_ptr<Node> right; // And, Or
};

using NodePtr = std::unique_ptr<Node>;

NodePtr make_binary(Node::Type type, NodePtr left, NodePtr right) {
    auto node = std::make_unique<Node>(type);
    node->left = std::move(left);
    node->right = std::move(right);
    return node;
}

NodePtr make_not(NodePtr child) {
    auto node = std::make_unique<Node>(Node::Type::Not);
    node->left = std::move(child);
    return node;
}

// ------------------------------------------------------------------- parser
//
//   or      := and { "or" and }
//   and     := not { ("and" | "not") not }     // "A not B" == "A and not B"
//   not     := "not" not | primary
//   primary := TERM | "(" or ")"
//
// Recursive descent. Every failure path sets a message and returns nullptr;
// ownership is handled by unique_ptr so nothing leaks on error.
class Parser {
public:
    explicit Parser(const std::vector<std::string>& tokens) : tokens_(tokens) {}

    NodePtr parse() {
        NodePtr root = parse_or();
        if (root && pos_ < tokens_.size()) {
            fail("unexpected '" + tokens_[pos_] +
                 "' (put AND, OR or NOT between terms)");
            return nullptr;
        }
        return root;
    }

    const std::string& error() const { return error_; }

private:
    bool at(const char* word) const {
        return pos_ < tokens_.size() && tokens_[pos_] == word;
    }

    void fail(std::string message) {
        if (error_.empty()) error_ = std::move(message);
    }

    NodePtr parse_or() {
        NodePtr left = parse_and();
        if (!left) return nullptr;
        while (at("or")) {
            ++pos_;
            NodePtr right = parse_and();
            if (!right) return nullptr;
            left = make_binary(Node::Type::Or, std::move(left), std::move(right));
        }
        return left;
    }

    NodePtr parse_and() {
        NodePtr left = parse_not();
        if (!left) return nullptr;
        for (;;) {
            if (at("and")) {
                ++pos_;
                NodePtr right = parse_not();
                if (!right) return nullptr;
                left = make_binary(Node::Type::And, std::move(left),
                                   std::move(right));
            } else if (at("not")) {
                ++pos_;
                NodePtr child = parse_not();
                if (!child) return nullptr;
                left = make_binary(Node::Type::And, std::move(left),
                                   make_not(std::move(child)));
            } else {
                return left;
            }
        }
    }

    NodePtr parse_not() {
        if (at("not")) {
            ++pos_;
            NodePtr child = parse_not();
            if (!child) return nullptr;
            return make_not(std::move(child));
        }
        return parse_primary();
    }

    NodePtr parse_primary() {
        if (pos_ >= tokens_.size()) {
            fail("unexpected end of query");
            return nullptr;
        }
        const std::string& token = tokens_[pos_];
        if (token == "(") {
            ++pos_;
            NodePtr inner = parse_or();
            if (!inner) return nullptr;
            if (!at(")")) {
                fail("missing closing parenthesis");
                return nullptr;
            }
            ++pos_;
            return inner;
        }
        if (token == ")") {
            fail("unexpected ')'");
            return nullptr;
        }
        if (token == "and" || token == "or") {
            fail("operator '" + token + "' needs a term on both sides");
            return nullptr;
        }
        ++pos_;
        return std::make_unique<Node>(Node::Type::Term, token);
    }

    const std::vector<std::string>& tokens_;
    std::size_t pos_ = 0;
    std::string error_;
};

// --------------------------------------------------------------- evaluation

using DocIds = std::vector<int>;  // always sorted ascending

DocIds intersect(const DocIds& a, const DocIds& b) {
    DocIds out;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                          std::back_inserter(out));
    return out;
}

DocIds unite(const DocIds& a, const DocIds& b) {
    DocIds out;
    std::set_union(a.begin(), a.end(), b.begin(), b.end(),
                   std::back_inserter(out));
    return out;
}

DocIds subtract(const DocIds& a, const DocIds& b) {
    DocIds out;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(),
                        std::back_inserter(out));
    return out;
}

// Evaluates the tree to the sorted set of matching document ids using posting
// list set operations. The "all documents" universe is only materialised if a
// NOT cannot be expressed as a set difference (e.g. a bare "NOT x").
class Evaluator {
public:
    explicit Evaluator(const InvertedIndex& index) : index_(index) {}

    DocIds evaluate(const Node& node) {
        switch (node.type) {
            case Node::Type::Term:
                return index_.documents_containing(node.term);
            case Node::Type::Or:
                return unite(evaluate(*node.left), evaluate(*node.right));
            case Node::Type::Not:
                return subtract(universe(), evaluate(*node.left));
            case Node::Type::And: {
                const Node& l = *node.left;
                const Node& r = *node.right;
                // A AND NOT B  ->  A \ B
                if (r.type == Node::Type::Not)
                    return subtract(evaluate(l), evaluate(*r.left));
                if (l.type == Node::Type::Not)
                    return subtract(evaluate(r), evaluate(*l.left));
                return intersect(evaluate(l), evaluate(r));
            }
        }
        return {};
    }

private:
    const DocIds& universe() {
        if (!have_universe_) {
            universe_ = index_.all_document_ids();
            have_universe_ = true;
        }
        return universe_;
    }

    const InvertedIndex& index_;
    DocIds universe_;
    bool have_universe_ = false;
};

// Terms that can contribute to relevance: those not under an odd number of NOTs.
void collect_positive_terms(const Node& node, bool negated,
                            std::vector<std::string>& out) {
    switch (node.type) {
        case Node::Type::Term:
            if (!negated) out.push_back(node.term);
            return;
        case Node::Type::Not:
            collect_positive_terms(*node.left, !negated, out);
            return;
        case Node::Type::And:
        case Node::Type::Or:
            collect_positive_terms(*node.left, negated, out);
            collect_positive_terms(*node.right, negated, out);
            return;
    }
}

}  // namespace

// ------------------------------------------------------------ QueryProcessor

QueryProcessor::QueryProcessor(const Tokenizer& tokenizer,
                               const InvertedIndex& index)
    : tokenizer_(tokenizer), index_(index) {}

std::vector<SearchResult> QueryProcessor::search(const std::string& query,
                                                 std::size_t top_k) const {
    return index_.search(tokenizer_.tokenize(query), top_k);
}

std::vector<SearchResult> QueryProcessor::phrase_search(
    const std::string& query, std::size_t top_k) const {
    return index_.phrase_search(tokenizer_.tokenize(query), top_k);
}

// Splits on whitespace and parentheses, then normalises each word with the
// same Tokenizer used for documents. A word that normalises to several tokens
// (e.g. "state-of-the-art") becomes a parenthesised AND of those tokens.
std::vector<std::string> QueryProcessor::tokenize_boolean_query(
    const std::string& query) const {
    std::vector<std::string> tokens;
    std::string word;

    auto flush_word = [&]() {
        if (word.empty()) return;
        const std::vector<std::string> parts = tokenizer_.tokenize(word);
        word.clear();
        if (parts.size() == 1) {
            tokens.push_back(parts[0]);
        } else if (parts.size() > 1) {
            tokens.push_back("(");
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) tokens.push_back("and");
                tokens.push_back(parts[i]);
            }
            tokens.push_back(")");
        }
    };

    for (char c : query) {
        if (tokens.size() > kMaxBooleanQueryTokens) break;  // rejected later
        if (c == '(' || c == ')') {
            flush_word();
            tokens.push_back(std::string(1, c));
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                   c == '\f' || c == '\v') {
            flush_word();
        } else {
            word += c;
        }
    }
    flush_word();
    return tokens;
}

std::vector<SearchResult> QueryProcessor::boolean_search(
    const std::string& query, std::size_t top_k, std::string* error) const {
    if (error) error->clear();
    auto reject = [&](const std::string& message) {
        if (error) *error = message;
        return std::vector<SearchResult>{};
    };

    if (top_k == 0) return {};

    const std::vector<std::string> tokens = tokenize_boolean_query(query);
    if (tokens.empty()) return reject("empty query");
    if (tokens.size() > kMaxBooleanQueryTokens) {
        return reject("query too long (max " +
                      std::to_string(kMaxBooleanQueryTokens) + " tokens)");
    }

    Parser parser(tokens);
    const NodePtr root = parser.parse();
    if (!root) return reject(parser.error());

    Evaluator evaluator(index_);
    const DocIds matches = evaluator.evaluate(*root);

    std::vector<std::string> ranking_terms;
    collect_positive_terms(*root, /*negated=*/false, ranking_terms);
    return index_.rank_candidates(ranking_terms, matches, top_k);
}
