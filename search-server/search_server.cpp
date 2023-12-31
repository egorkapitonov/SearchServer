#include "search_server.h"

void SearchServer::AddDocument(int document_id, std::string_view document, DocumentStatus status, const std::vector<int>& ratings) {
    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw std::invalid_argument("Invalid document_id");
    }
    auto [doc_id_, doc_data_] = documents_.emplace(document_id, DocumentData{ std::string(document),  ComputeAverageRating(ratings), status });
    document_ids_.push_back(document_id);

    const auto words = SplitIntoWordsNoStop(doc_id_->second.data_string);

    const double inv_word_count = 1.0 / words.size();
    for (auto word : words) {
        word_to_document_freqs_[word][document_id] += inv_word_count;
        document_to_word_freqs_[document_id][word] += inv_word_count;
    }
}

//FindTopDocuments(raw_query, status)
//FindTopDocuments(raw_query)
std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
        });
}
std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query) const {
    return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}
// 1: Замена GetDocumentId на методы begin и end
std::vector<int>::const_iterator SearchServer::begin() const {
    return document_ids_.begin();
}
std::vector<int>::const_iterator SearchServer::end() const {
    return document_ids_.end();
}
// 2: Метод получения частот слов по id документа: 
const std::map<std::string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {
    //Если документа не существует, возвратите ссылку на пустой map.
    static std::map<std::string_view, double> word_freqs;
    if (document_to_word_freqs_.count(document_id) == 0) {
        return word_freqs;
    }
    else {
        word_freqs = document_to_word_freqs_.at(document_id);
        return word_freqs;
    }
}

// 3: Метод удаления документов из поискового сервера
void SearchServer::RemoveDocument(int document_id) {
    RemoveDocument(std::execution::seq,
        document_id);
}

// 3: Однопоточная версия метода RemoveDocument
void SearchServer::RemoveDocument(const std::execution::sequenced_policy&, int document_id) {
    auto find_remove = std::find(document_ids_.begin(), document_ids_.end(), document_id);
    
    std::vector<std::string_view> word(document_to_word_freqs_.at(document_id).size());
    transform(std::execution::seq, document_to_word_freqs_.at(document_id).begin(), document_to_word_freqs_.at(document_id).end(), word.begin(), [](auto i) {
        return i.first;
    });
    
    if (find_remove == document_ids_.end()) {
        return;
    } else {
        document_ids_.erase(find_remove);
    }

    document_to_word_freqs_.erase(document_id);
    documents_.erase(document_id);
    
    std::for_each(std::execution::seq, word.begin(), word.end(), [&](auto& find_remove) {
        word_to_document_freqs_.at(find_remove).erase(document_id);
        });
    /*
    std::for_each(std::execution::seq, document_to_word_freqs_.at(document_id).begin(), document_to_word_freqs_.at(document_id).end(), [&](auto [word, frequency]) {
        word_to_document_freqs_.at(word).erase(document_id);
        });
    */
}

// 3: Многопоточная версия метода RemoveDocument
void SearchServer::RemoveDocument(const std::execution::parallel_policy&, int document_id) {
    auto find_remove = std::find(std::execution::par, document_ids_.begin(), document_ids_.end(), document_id);
    std::vector<std::string_view> word(document_to_word_freqs_.at(document_id).size());
    transform(std::execution::par, document_to_word_freqs_.at(document_id).begin(), document_to_word_freqs_.at(document_id).end(), word.begin(), [](auto i) {
        return i.first;
    });
    
    if (find_remove == document_ids_.end()) {
        return;
    } else {
        document_ids_.erase(find_remove);
    }

    document_to_word_freqs_.erase(document_id);
    documents_.erase(document_id);

    std::for_each(std::execution::par, word.begin(), word.end(), [&](auto& find_remove) {
        word_to_document_freqs_.at(find_remove).erase(document_id);
        });
}
// MatchDocument 1
std::tuple<std::vector<std::string_view>, DocumentStatus>  SearchServer::MatchDocument(const std::execution::sequenced_policy&, std::string_view raw_query, int document_id) const {

    const auto query = ParseQuery(raw_query, std::execution::seq);

    std::vector<std::string_view> matched_words;
    for (auto word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(word).count(document_id)) {
            return { matched_words, documents_.at(document_id).status };
        }
    }
    for (auto word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(word).count(document_id)) {
            matched_words.push_back(word);
        }
    }

    return { matched_words, documents_.at(document_id).status };
}

// MatchDocument 2
std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(std::string_view raw_query, int document_id) const {
    return MatchDocument(std::execution::seq, raw_query, document_id);
}
// MatchDocument 3
std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const std::execution::parallel_policy&, std::string_view raw_query, int document_id) const {
    const auto query = ParseQuery(raw_query, std::execution::seq);

    std::vector<std::string_view> matched_words(query.plus_words.size());

    const auto check = [this, document_id](std::string_view word) {
        const auto find_word = word_to_document_freqs_.find(word);
        return find_word != word_to_document_freqs_.end() && find_word->second.count(document_id);
    };

    if (std::any_of(std::execution::par, query.minus_words.begin(), query.minus_words.end(), check)) {
        return { {}, documents_.at(document_id).status };
    }

    auto end = std::copy_if(std::execution::par, query.plus_words.begin(), query.plus_words.end(), matched_words.begin(), check);

    std::sort(matched_words.begin(), end);
    end = std::unique(std::execution::par, matched_words.begin(), end);
    matched_words.erase(end, matched_words.end());

    return { matched_words, documents_.at(document_id).status };
}

bool SearchServer::IsStopWord(std::string_view word) const {
    return stop_words_.count(word) > 0;
}

bool SearchServer::IsValidWord(std::string_view word) {
    return std::none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
        });
}

std::vector<std::string_view> SearchServer::SplitIntoWordsNoStop(std::string_view text) const {
    std::vector<std::string_view> words;
    for (std::string_view word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw std::invalid_argument("Word " + std::string(word) + " is invalid");
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

int SearchServer::ComputeAverageRating(const std::vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    int rating_sum = 0;
    for (const int rating : ratings) {
        rating_sum += rating;
    }
    return rating_sum / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(std::string_view text) const {
    if (text.empty()) {
        throw std::invalid_argument("Query word is empty");
    }

    bool is_minus = false;
    if (text[0] == '-') {
        is_minus = true;
        text = text.substr(1);
    }

    if (text.empty() || text[0] == '-' || !IsValidWord(text)) {
        throw std::invalid_argument("Query word " + std::string(text) + " is invalid");
    }
    return { text, is_minus, IsStopWord(text) };
}

SearchServer::Query SearchServer::ParseQuery(std::string_view text) const {

    Query result;

    for (std::string_view word : SplitIntoWords(text)) {
        const auto query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.push_back(query_word.data);
            }
            else {
                result.plus_words.push_back(query_word.data);
            }
        }
    }
    return result;
}
SearchServer::Query SearchServer::ParseQuery(std::string_view text, const std::execution::sequenced_policy&) const {

    Query result;
    result = ParseQuery(text);

    std::sort(std::execution::seq, result.minus_words.begin(), result.minus_words.end());
    std::sort(std::execution::seq, result.plus_words.begin(), result.plus_words.end());

    result.minus_words.erase(std::unique(result.minus_words.begin(), result.minus_words.end()), result.minus_words.end());
    result.plus_words.erase(std::unique(result.plus_words.begin(), result.plus_words.end()), result.plus_words.end());

    return result;
}
double SearchServer::ComputeWordInverseDocumentFreq(std::string_view word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}