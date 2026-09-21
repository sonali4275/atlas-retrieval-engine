#include "query_processor.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct BooleanNode {

    enum class Type {
        TERM,
        AND,
        OR,
        NOT
    };

    Type type;

    std::string term;

    BooleanNode* left;
    BooleanNode* right;

    BooleanNode(
        Type node_type,
        const std::string& node_term = ""
    )
        : type(node_type),
          term(node_term),
          left(nullptr),
          right(nullptr) {
    }
};

void delete_tree(BooleanNode* node) {

    if (node == nullptr) {
        return;
    }

    delete_tree(node->left);
    delete_tree(node->right);

    delete node;
}

} // namespace


QueryProcessor::QueryProcessor(
    const Tokenizer& tokenizer,
    const InvertedIndex& index
)
    : tokenizer_(tokenizer),
      index_(index) {
}


/*
 * ============================================================
 * NORMAL SEARCH
 * ============================================================
 */

std::vector<SearchResult> QueryProcessor::search(
    const std::string& query,
    std::size_t top_k
) const {

    const std::vector<std::string> query_tokens =
        tokenizer_.tokenize(query);

    return index_.search(
        query_tokens,
        top_k
    );
}


/*
 * ============================================================
 * PHRASE SEARCH
 * ============================================================
 */

std::vector<SearchResult>
QueryProcessor::phrase_search(
    const std::string& query,
    std::size_t top_k
) const {

    const std::vector<std::string> query_tokens =
        tokenizer_.tokenize(query);

    return index_.phrase_search(
        query_tokens,
        top_k
    );
}


/*
 * ============================================================
 * BOOLEAN OPERATOR
 * ============================================================
 */

bool QueryProcessor::is_boolean_operator(
    const std::string& token
) const {

    return token == "and" ||
           token == "or" ||
           token == "not";
}


/*
 * ============================================================
 * BOOLEAN QUERY TOKENIZER
 * ============================================================
 *
 * Example:
 *
 * vector AND (search OR retrieval)
 *
 * becomes:
 *
 * vector
 * and
 * (
 * search
 * or
 * retrieval
 * )
 *
 * ============================================================
 */

std::vector<std::string>
QueryProcessor::tokenize_boolean_query(
    const std::string& query
) const {

    std::vector<std::string> tokens;

    std::string current;

    auto flush_current =
        [&]() {

            if (current.empty()) {
                return;
            }

            std::string normalized;

            for (char character : current) {

                if (std::isalnum(
                        static_cast<unsigned char>(
                            character
                        )
                    )) {

                    normalized +=
                        static_cast<char>(
                            std::tolower(
                                static_cast<unsigned char>(
                                    character
                                )
                            )
                        );
                }
            }

            if (!normalized.empty()) {
                tokens.push_back(normalized);
            }

            current.clear();
        };

    for (char character : query) {

        if (character == '(' ||
            character == ')') {

            flush_current();

            tokens.push_back(
                std::string(1, character)
            );

            continue;
        }

        if (std::isspace(
                static_cast<unsigned char>(
                    character
                )
            )) {

            flush_current();

            continue;
        }

        current += character;
    }

    flush_current();

    return tokens;
}


/*
 * ============================================================
 * BOOLEAN SEARCH
 * ============================================================
 *
 * Grammar:
 *
 * expression
 *     = OR
 *
 * OR
 *     = AND { OR AND }
 *
 * AND
 *     = NOT { AND NOT }
 *
 * NOT
 *     = "not" NOT
 *       | primary
 *
 * primary
 *     = TERM
 *       | "(" expression ")"
 *
 * In addition:
 *
 * A NOT B
 *
 * is interpreted as:
 *
 * A AND (NOT B)
 *
 * Therefore:
 *
 * vector NOT databases
 *
 * means:
 *
 * vector AND NOT databases
 *
 * ============================================================
 */

std::vector<SearchResult>
QueryProcessor::boolean_search(
    const std::string& query,
    std::size_t top_k
) const {

    if (query.empty() ||
        top_k == 0) {

        return {};
    }

    const std::vector<std::string> tokens =
        tokenize_boolean_query(query);

    if (tokens.empty()) {
        return {};
    }

    std::size_t position = 0;


    /*
     * Parser functions.
     */

    std::function<BooleanNode*()> parse_expression;
    std::function<BooleanNode*()> parse_or;
    std::function<BooleanNode*()> parse_and;
    std::function<BooleanNode*()> parse_not;
    std::function<BooleanNode*()> parse_primary;


    /*
     * ========================================================
     * PRIMARY
     * ========================================================
     */

    parse_primary =
        [&]() -> BooleanNode* {

        if (position >= tokens.size()) {
            return nullptr;
        }

        const std::string& token =
            tokens[position];


        /*
         * Parenthesized expression.
         */

        if (token == "(") {

            ++position;

            BooleanNode* node =
                parse_expression();

            if (position >= tokens.size() ||
                tokens[position] != ")") {

                delete_tree(node);

                return nullptr;
            }

            ++position;

            return node;
        }


        /*
         * Closing parenthesis cannot start
         * an expression.
         */

        if (token == ")") {
            return nullptr;
        }


        /*
         * Operators cannot directly be terms.
         */

        if (is_boolean_operator(token)) {
            return nullptr;
        }


        ++position;

        return new BooleanNode(
            BooleanNode::Type::TERM,
            token
        );
    };


    /*
     * ========================================================
     * NOT
     * ========================================================
     *
     * Unary NOT has the highest precedence.
     *
     * Example:
     *
     * NOT database
     *
     * ========================================================
     */

    parse_not =
        [&]() -> BooleanNode* {

        if (position < tokens.size() &&
            tokens[position] == "not") {

            ++position;

            BooleanNode* child =
                parse_not();

            if (child == nullptr) {
                return nullptr;
            }

            BooleanNode* node =
                new BooleanNode(
                    BooleanNode::Type::NOT
                );

            node->right = child;

            return node;
        }

        return parse_primary();
    };


    /*
     * ========================================================
     * AND
     * ========================================================
     *
     * Handles:
     *
     * A AND B
     *
     * A NOT B
     *
     * A AND NOT B
     *
     * A AND B AND C
     *
     * ========================================================
     */

    parse_and =
        [&]() -> BooleanNode* {

        BooleanNode* left =
            parse_not();

        if (left == nullptr) {
            return nullptr;
        }


        while (position < tokens.size()) {

            /*
             * Normal AND.
             */

            if (tokens[position] == "and") {

                ++position;

                BooleanNode* right =
                    parse_not();

                if (right == nullptr) {

                    delete_tree(left);

                    return nullptr;
                }

                BooleanNode* parent =
                    new BooleanNode(
                        BooleanNode::Type::AND
                    );

                parent->left = left;
                parent->right = right;

                left = parent;

                continue;
            }


            /*
             * Binary NOT.
             *
             * A NOT B
             *
             * becomes:
             *
             * A AND (NOT B)
             */

            if (tokens[position] == "not") {

                ++position;

                BooleanNode* right =
                    parse_not();

                if (right == nullptr) {

                    delete_tree(left);

                    return nullptr;
                }

                BooleanNode* not_node =
                    new BooleanNode(
                        BooleanNode::Type::NOT
                    );

                not_node->right = right;

                BooleanNode* parent =
                    new BooleanNode(
                        BooleanNode::Type::AND
                    );

                parent->left = left;
                parent->right = not_node;

                left = parent;

                continue;
            }

            break;
        }

        return left;
    };


    /*
     * ========================================================
     * OR
     * ========================================================
     */

    parse_or =
        [&]() -> BooleanNode* {

        BooleanNode* left =
            parse_and();

        if (left == nullptr) {
            return nullptr;
        }

        while (position < tokens.size() &&
               tokens[position] == "or") {

            ++position;

            BooleanNode* right =
                parse_and();

            if (right == nullptr) {

                delete_tree(left);

                return nullptr;
            }

            BooleanNode* parent =
                new BooleanNode(
                    BooleanNode::Type::OR
                );

            parent->left = left;
            parent->right = right;

            left = parent;
        }

        return left;
    };


    /*
     * ========================================================
     * EXPRESSION
     * ========================================================
     */

    parse_expression =
        [&]() -> BooleanNode* {

        return parse_or();
    };


    /*
     * ========================================================
     * BUILD EXPRESSION TREE
     * ========================================================
     */

    BooleanNode* root =
        parse_expression();


    /*
     * Invalid expression.
     */

    if (root == nullptr ||
        position != tokens.size()) {

        delete_tree(root);

        return {};
    }


    /*
     * ========================================================
     * COLLECT SEARCH TERMS
     * ========================================================
     */

    std::unordered_set<std::string>
        unique_terms;

    std::function<void(const BooleanNode*)>
        collect_terms;

    collect_terms =
        [&](const BooleanNode* node) {

        if (node == nullptr) {
            return;
        }

        if (node->type ==
            BooleanNode::Type::TERM) {

            unique_terms.insert(
                node->term
            );

            return;
        }

        collect_terms(node->left);
        collect_terms(node->right);
    };

    collect_terms(root);


    if (unique_terms.empty()) {

        delete_tree(root);

        return {};
    }


    std::vector<std::string> terms(
        unique_terms.begin(),
        unique_terms.end()
    );


    /*
     * ========================================================
     * GET BM25 CANDIDATES
     * ========================================================
     */

    const std::size_t candidate_limit =
        static_cast<std::size_t>(
            index_.document_count()
        );

    std::vector<SearchResult> candidates =
        index_.search(
            terms,
            candidate_limit
        );


    /*
     * ========================================================
     * BUILD TERM -> DOCUMENT MAP
     * ========================================================
     */

    std::unordered_map<
        std::string,
        std::unordered_set<int>
    > term_documents;

    for (const std::string& term : terms) {

        std::vector<SearchResult>
            term_results =
                index_.search(
                    {term},
                    candidate_limit
                );

        for (const auto& result :
             term_results) {

            term_documents[term].insert(
                result.document_id
            );
        }
    }


    /*
     * ========================================================
     * EVALUATE BOOLEAN TREE
     * ========================================================
     */

    std::function<bool(
        const BooleanNode*,
        int
    )> evaluate;

    evaluate =
        [&](const BooleanNode* node,
            int document_id) -> bool {

        if (node == nullptr) {
            return false;
        }


        /*
         * TERM
         */

        if (node->type ==
            BooleanNode::Type::TERM) {

            auto term_it =
                term_documents.find(
                    node->term
                );

            if (term_it ==
                term_documents.end()) {

                return false;
            }

            return
                term_it->second.find(
                    document_id
                ) != term_it->second.end();
        }


        /*
         * AND
         */

        if (node->type ==
            BooleanNode::Type::AND) {

            return
                evaluate(
                    node->left,
                    document_id
                )
                &&
                evaluate(
                    node->right,
                    document_id
                );
        }


        /*
         * OR
         */

        if (node->type ==
            BooleanNode::Type::OR) {

            return
                evaluate(
                    node->left,
                    document_id
                )
                ||
                evaluate(
                    node->right,
                    document_id
                );
        }


        /*
         * NOT
         */

        if (node->type ==
            BooleanNode::Type::NOT) {

            return !evaluate(
                node->right,
                document_id
            );
        }

        return false;
    };


    /*
     * ========================================================
     * FILTER CANDIDATES
     * ========================================================
     */

    std::vector<SearchResult> results;

    for (const auto& candidate :
         candidates) {

        if (evaluate(
                root,
                candidate.document_id
            )) {

            results.push_back(candidate);
        }
    }


    /*
     * ========================================================
     * BM25 RANKING
     * ========================================================
     */

    std::sort(
        results.begin(),
        results.end(),
        [](const SearchResult& a,
           const SearchResult& b) {

            if (a.score != b.score) {
                return a.score > b.score;
            }

            return
                a.document_id <
                b.document_id;
        }
    );


    /*
     * ========================================================
     * TOP-K
     * ========================================================
     */

    if (results.size() > top_k) {
        results.resize(top_k);
    }


    /*
     * ========================================================
     * CLEAN UP
     * ========================================================
     */

    delete_tree(root);

    return results;
}