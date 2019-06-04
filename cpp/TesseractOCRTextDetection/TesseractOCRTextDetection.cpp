/******************************************************************************
 * NOTICE                                                                     *
 *                                                                            *
 * This software (or technical data) was produced for the U.S. Government     *
 * under contract, and is subject to the Rights in Data-General Clause        *
 * 52.227-14, Alt. IV (DEC 2007).                                             *
 *                                                                            *
 * Copyright 2019 The MITRE Corporation. All Rights Reserved.                 *
 ******************************************************************************/

/******************************************************************************
 * Copyright 2019 The MITRE Corporation                                       *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License");            *
 * you may not use this file except in compliance with the License.           *
 * You may obtain a copy of the License at                                    *
 *                                                                            *
 *    http://www.apache.org/licenses/LICENSE-2.0                              *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 ******************************************************************************/

#include <map>
#include <iostream>
#include <boost/regex.hpp>
#include <boost/locale.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/filesystem.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <log4cxx/logmanager.h>
#include <log4cxx/xml/domconfigurator.h>
#include <algorithm>
#include <cctype>
#include "TesseractOCRTextDetection.h"
#include "detectionComponentUtils.h"
#include <Utils.h>
#include <fstream>
#include "JSON.h"
#include "MPFSimpleConfigLoader.h"
#include <tesseract/baseapi.h>
#include <tesseract/osdetect.h>
#include <unicharset.h>
#include <Magick++.h>

using namespace MPF;
using namespace COMPONENT;
using namespace std;
using log4cxx::Logger;
using log4cxx::xml::DOMConfigurator;

bool TesseractOCRTextDetection::Init() {
    // Set global C++ locale.
    // Required for boost function calls.
    // Also overwrites C locale:
    // https://en.cppreference.com/w/cpp/locale/locale/global.
    boost::locale::generator gen;
    locale loc = gen("");
    locale::global(loc);

    // Reset C locale back to default.
    // Required for Tesseract API calls:
    // https://github.com/tesseract-ocr/tesseract/commit/3292484f67af8bdda23aa5e510918d0115785291
    setlocale(LC_ALL, "C");

    // Determine where the executable is running.
    string run_dir = GetRunDirectory();
    if (run_dir == "") {
        run_dir = ".";
    }
    string plugin_path = run_dir + "/TesseractOCRTextDetection";
    string config_path = plugin_path + "/config";
    cout << "looking for logger at " << plugin_path + "/config/Log4cxxConfig.xml" << endl;
    log4cxx::xml::DOMConfigurator::configure(plugin_path + "/config/Log4cxxConfig.xml");
    hw_logger_ = log4cxx::Logger::getLogger("TesseractOCRTextDetection");

    LOG4CXX_DEBUG(hw_logger_, "Plugin path: " << plugin_path);
    LOG4CXX_INFO(hw_logger_, "Initializing TesseractOCRTextDetection");

    reg_table[L"\\\\d"] = L"[[:digit:]]";
    reg_table[L"\\\\l"] = L"[[:lower:]]";
    reg_table[L"\\\\s"] = L"[[:space:]]";
    reg_table[L"\\\\u"] = L"[[:upper:]]";
    reg_table[L"\\\\w"] = L"[[:word:]]";
    reg_table[L"\\\\D"] = L"[^[:digit:]]";
    reg_table[L"\\\\L"] = L"[^[:lower:]]";
    reg_table[L"\\\\S"] = L"[^[:space:]]";
    reg_table[L"\\\\U"] = L"[^[:upper:]]";
    reg_table[L"\\\\W"] = L"[^[:word:]]";
    reg_table[L"\\b"] = L"\\b";
    reg_table[L"\\B"] = L"\\B";
    reg_table[L"\\p"] = L"\\p";
    reg_table[L"\\P"] = L"\\P";


    set_default_parameters();
    default_ocr_fset.model_dir = "";

    // Once this is done - parameters will be set and set_read_config_parameters() can be called again to revert back
    // to the params read at initialization.
    string config_params_path = config_path + "/mpfOCR.ini";
    int rc = LoadConfig(config_params_path, parameters);
    if (rc) {
        LOG4CXX_ERROR(hw_logger_, "Could not parse config file: " << config_params_path);
        return false;
    }
    set_read_config_parameters();

    LOG4CXX_INFO(hw_logger_, "INITIALIZED COMPONENT.");
    return true;
}

// Ensure this is idempotent because it's called by the destructor.
bool TesseractOCRTextDetection::Close() {
    for (auto const &element : tess_api_map) {
        tesseract::TessBaseAPI *tess_api = element.second;
        tess_api->End();
        delete tess_api;
    }
    tess_api_map.clear();
    return true;
}

TesseractOCRTextDetection::~TesseractOCRTextDetection() {
    Close();
}

string TesseractOCRTextDetection::GetDetectionType() {
    return "TEXT";
}

bool TesseractOCRTextDetection::Supports(MPFDetectionDataType data_type) {

    // Supports images and documents, no audio or video data types.
    return data_type == MPFDetectionDataType::IMAGE || data_type == MPFDetectionDataType::UNKNOWN;
}

/*
 * Called during Init.
 * Initialize default parameters.
 */
void TesseractOCRTextDetection::set_default_parameters() {
    default_ocr_fset.psm = 3;
    default_ocr_fset.oem = 3;
    default_ocr_fset.sharpen = 1.0;
    default_ocr_fset.scale = 2.4;
    default_ocr_fset.threshold_check = true;
    default_ocr_fset.hist_check = true;
    default_ocr_fset.num_only_ok = true;
    default_ocr_fset.min_word_len = 3;
    default_ocr_fset.hist_min_char = 45;
    default_ocr_fset.excess_eng_symbols = 0.35;
    default_ocr_fset.excess_non_eng_symbols = 0.10;
    default_ocr_fset.vowel_min = 0.10;
    default_ocr_fset.vowel_max = 0.95;
    default_ocr_fset.correl_limit = 0.52;
    default_ocr_fset.invert = false;
    default_ocr_fset.enable_sharpen = false;
    default_ocr_fset.enable_rescale = true;
    default_ocr_fset.enable_otsu_thrs = false;
    default_ocr_fset.enable_adaptive_thrs = false;
    default_ocr_fset.tesseract_lang = "eng";
    default_ocr_fset.adaptive_thrs_pixel = 11;
    default_ocr_fset.adaptive_thrs_c = 0;
    default_ocr_fset.enable_osd = true;
    default_ocr_fset.combine_detected_scripts = true;
    default_ocr_fset.min_script_confidence = 2.0;
    default_ocr_fset.min_script_score = 50.0;
    default_ocr_fset.min_orientation_confidence = 2.0;
    default_ocr_fset.max_scripts = 1;
    default_ocr_fset.max_text_tracks = 0;
    default_ocr_fset.min_secondary_script_thrs = 0.80;

}

/*
 * Called during Init.
 * Copy over parameters values from .ini file.
 */
void TesseractOCRTextDetection::set_read_config_parameters() {

    if (parameters.contains("SHARPEN")) {
        default_ocr_fset.sharpen = parameters["SHARPEN"].toDouble();
    }
    if (parameters.contains("SCALE")) {
        default_ocr_fset.scale = parameters["SCALE"].toDouble();
    }
    if (parameters.contains("INVERT")) {
        default_ocr_fset.invert = (parameters["INVERT"].toInt() > 0);
    }
    if (parameters.contains("THRS_FILTER")) {
        default_ocr_fset.threshold_check = (parameters["THRS_FILTER"].toInt() > 0);
    }
    if (parameters.contains("HIST_FILTER")) {
        default_ocr_fset.hist_check = (parameters["HIST_FILTER"].toInt() > 0);
    }
    if (parameters.contains("NUM_ONLY")) {
        default_ocr_fset.num_only_ok = (parameters["NUM_ONLY"].toInt() > 0);
    }
    if (parameters.contains("MIN_WORD_LEN")) {
        default_ocr_fset.min_word_len = parameters["MIN_WORD_LEN"].toInt();
    }
    if (parameters.contains("MIN_HIST_SIZE")) {
        default_ocr_fset.hist_min_char = parameters["MIN_HIST_SIZE"].toInt();
    }
    if (parameters.contains("MIN_HIST_SCORE")) {
        default_ocr_fset.correl_limit = parameters["MIN_HIST_SCORE"].toDouble();
    }
    if (parameters.contains("MAX_ENG_PNCT")) {
        default_ocr_fset.excess_eng_symbols = parameters["MAX_ENG_PNCT"].toDouble();
    }
    if (parameters.contains("MAX_FRN_CHAR")) {
        default_ocr_fset.excess_non_eng_symbols = parameters["MAX_FRN_CHAR"].toDouble();
    }
    if (parameters.contains("VOWEL_MIN")) {
        default_ocr_fset.vowel_min = parameters["VOWEL_MIN"].toDouble();
    }
    if (parameters.contains("VOWEL_MAX")) {
        default_ocr_fset.vowel_max = parameters["VOWEL_MAX"].toDouble();
    }
    if (parameters.contains("ENABLE_SHARPEN")) {
        default_ocr_fset.enable_sharpen = (parameters["ENABLE_SHARPEN"].toInt() > 0);
    }
    if (parameters.contains("ENABLE_RESCALE")) {
        default_ocr_fset.enable_rescale = (parameters["ENABLE_RESCALE"].toInt() > 0);
    }
    if (parameters.contains("ENABLE_OTSU_THRS")) {
        default_ocr_fset.enable_otsu_thrs = (parameters["ENABLE_OTSU_THRS"].toInt() > 0);
    }
    if (parameters.contains("ENABLE_ADAPTIVE_THRS")) {
        default_ocr_fset.enable_adaptive_thrs = (parameters["ENABLE_ADAPTIVE_THRS"].toInt() > 0);
    }
    if (parameters.contains("ADAPTIVE_THRS_CONSTANT")) {
        default_ocr_fset.adaptive_thrs_c = parameters["ADAPTIVE_THRS_CONSTANT"].toDouble();
    }
    if (parameters.contains("ADAPTIVE_THRS_BLOCKSIZE")) {
        default_ocr_fset.adaptive_thrs_pixel = parameters["ADAPTIVE_THRS_BLOCKSIZE"].toInt();
    }
    if (parameters.contains("TESSERACT_LANGUAGE")) {
        default_ocr_fset.tesseract_lang = parameters["TESSERACT_LANGUAGE"].toStdString();
    }
    if (parameters.contains("TESSERACT_PSM")) {
        default_ocr_fset.psm = parameters["TESSERACT_PSM"].toInt();
    }
    if (parameters.contains("TESSERACT_OEM")) {
        default_ocr_fset.oem = parameters["TESSERACT_OEM"].toInt();
    }
    if (parameters.contains("ENABLE_OSD_AUTOMATION")) {
        default_ocr_fset.enable_osd = (parameters["ENABLE_OSD_AUTOMATION"].toInt() > 0);
    }
    if (parameters.contains("COMBINE_OSD_SCRIPTS")) {
        default_ocr_fset.combine_detected_scripts = parameters["COMBINE_OSD_SCRIPTS"].toInt() > 0;
    }
    if (parameters.contains("MAX_OSD_SCRIPTS")) {
        default_ocr_fset.max_scripts = parameters["MAX_OSD_SCRIPTS"].toInt();
    }
    if (parameters.contains("MAX_TEXT_TRACKS")) {
        default_ocr_fset.max_text_tracks = parameters["MAX_TEXT_TRACKS"].toInt();
    }
    if (parameters.contains("MIN_OSD_SECONDARY_SCRIPT_THRESHOLD")) {
        default_ocr_fset.min_secondary_script_thrs = parameters["MIN_OSD_SECONDARY_SCRIPT_THRESHOLD"].toDouble();
    }
    if (parameters.contains("MIN_OSD_ROTATION_CONFIDENCE")) {
        default_ocr_fset.min_orientation_confidence = parameters["MIN_OSD_ROTATION_CONFIDENCE"].toDouble();
    }
    if (parameters.contains("MIN_OSD_PRIMARY_SCRIPT_CONFIDENCE")) {
        default_ocr_fset.min_script_confidence = parameters["MIN_OSD_PRIMARY_SCRIPT_CONFIDENCE"].toDouble();
    }
    if (parameters.contains("MIN_OSD_SCRIPT_SCORE")) {
        default_ocr_fset.min_script_score = parameters["MIN_OSD_SCRIPT_SCORE"].toDouble();
    }
}

/*
 * Counts whitespace, alphanumeric, non-english characters in string.
 */
TesseractOCRTextDetection::OCR_char_stats
TesseractOCRTextDetection::char_count(const wstring &s, const wstring &white_space, const wstring &eng_symbol,
                                      const wstring &eng_num) {

    TesseractOCRTextDetection::OCR_char_stats stats = {
            0,  // alphabet_count
            0,  // num_count
            0,  // whspace_count
            0,  // punct_count
            0,  // non_eng_count
            {0} // char_list
    };

    static locale locale("en_US");
    for (const wchar_t &c : s) {
        if (isspace(c, locale)) {
            stats.whspace_count++;
            continue;
        }

        if (ispunct(c, locale)) {
            stats.punct_count++;
            continue;
        }

        if (isdigit(c, locale)) {
            stats.num_count++;
            continue;
        }

        int x = tolower(c, locale) - L'a';
        if (x >= 0 && x <= 25) {
            stats.alphabet_count++;
            stats.char_list[x]++;
            continue;
        }
        stats.non_eng_count++;
    }
    return stats;
}

/*
 * Conduct filtering of results.
 * Reject/accept text based on char frequency/histogram comparison to english language.
 */
wstring TesseractOCRTextDetection::check_string(const wstring &s,
                                                const TesseractOCRTextDetection::OCR_filter_settings &ocrset) {

    bool num_only_ok = ocrset.num_only_ok;
    bool threshold_check = ocrset.threshold_check;
    bool hist_check = ocrset.hist_check;
    int min_word_len = ocrset.min_word_len;
    float excess_eng_symbols = ocrset.excess_eng_symbols;
    float vowel_min = ocrset.vowel_min;
    float vowel_max = ocrset.vowel_max;
    int hist_min_char = ocrset.hist_min_char;
    float correl_limit = ocrset.correl_limit;

    // The following are characters commonly used in the english language.
    // We should not penalize the OCR for detecting these.
    // Only start penalizing when they become excessive.

    // Allow white space to be ignored.
    wstring white_space = L" \n\t\f\v\r";

    // Common english characters and punctuation.
    // May need to penalize if these occur too frequently.
    // If a large portion of the sentences are composed of these characters,
    // it's likely gibberish so toss them out.
    wstring eng_symbol = L".,?!-()[]{}<>:;/@#$%^&*-+_='\\~\"";

    // Allow numbers by default.
    // Text could be from an academic source, or a phone number.
    wstring eng_num = L"0123456789";

    // Histogram of english characters
    float eng_list[] = {8.167, 1.492, 2.782, 4.253, 12.702, 2.228, 2.015, 6.094,
                        6.966, 0.153, 0.772, 4.025, 2.406, 6.749, 7.507, 1.929, 0.095,
                        5.987, 6.327, 9.056, 2.758, 0.978, 2.360, 0.150, 1.974, 0.074};

    // Histogram parameters:
    int nbins = 200;
    int histSize[] = {nbins};
    float range[] = {0, 100};
    const float *ranges[] = {range};
    int channels[] = {0};

    cv::Mat eng_mat = cv::Mat(26, 1, CV_32F, eng_list);
    cv::MatND eng_hist;
    cv::calcHist(&eng_mat, 1, channels, cv::Mat(), // do not use mask
                 eng_hist, 1, histSize, ranges);


    TesseractOCRTextDetection::OCR_char_stats results = char_count(s, white_space, eng_symbol, eng_num);
    int alphabet_count = results.alphabet_count;
    int num_count = results.num_count;
    int punct_count = results.punct_count;
    int non_eng_count = results.non_eng_count;
    int *char_list = results.char_list;
    float char_f_list[26] = {0};
    cv::Mat char_mat;
    cv::MatND char_hist;
    if (alphabet_count > 0) {
        for (int i = 0; i < 26; i++) {
            float f = (float) char_list[i] / (float) alphabet_count * 100.0;
            char_f_list[i] = f;
        }
        char_mat = cv::Mat(26, 1, CV_32F, char_f_list);
        cv::calcHist(&char_mat, 1, channels, cv::Mat(), // do not use mask
                     char_hist, 1, histSize, ranges);
    }


    if (threshold_check) {
        int total_eng_char = num_count + alphabet_count + punct_count;
        if (alphabet_count + num_count < min_word_len) {
            return L"";
        }

        float eng_symb_fraction = float(punct_count) / float(total_eng_char);
        if (eng_symb_fraction > excess_eng_symbols) {
            return L"";
        }

        float non_eng_fraction = float(non_eng_count) / float(total_eng_char + non_eng_count);
        if (non_eng_fraction > eng_symb_fraction) {
            return L"";
        }

        int max_wsize = 0;
        wstringstream iss(s);

        do {
            wstring subs;
            iss >> subs;
            if (subs.length() > max_wsize) {
                max_wsize = subs.length();
            }
        } while (iss);

        if (max_wsize < min_word_len) {
            return L"";
        }

        // Calculate vowel percentage and check if threshold is met.
        float vowel_percent = (float) (char_list[0] + char_list[4] + char_list[8]
                                       + char_list[14] + char_list[20] + char_list[24]) / (float) alphabet_count;
        if (vowel_percent < vowel_min || vowel_percent > vowel_max) {
            return L"";
        }

    }
    if (alphabet_count == 0) {
        if (num_only_ok) {
            return s;
        } else {
            return L"";
        }
    }
    if (hist_check && alphabet_count >= hist_min_char) {
        double result = abs(cv::compareHist(eng_hist, char_hist, CV_COMP_CORREL));
        if (result < correl_limit) {
            return L"";
        }
    }
    return s;
}

/*
 * Image preprocessing to improve text extraction.
 * Sharpens image.
 */
void TesseractOCRTextDetection::sharpen(cv::Mat &image, double weight = 1.0) {
    cv::Mat blurred, mask;
    cv::blur(image, blurred, cv::Size(2, 2));
    cv::threshold(blurred, mask, 48, 1, cv::THRESH_BINARY);
    cv::multiply(blurred, mask, blurred);
    cv::addWeighted(image, 1.0 + weight, blurred, -1.0, 0, image);
}

/*
 * Helper function for string processing.
 */
inline wstring to_lowercase(const wstring &data) {
    wstring d2(data);
    d2 = boost::locale::normalize(d2);
    d2 = boost::locale::to_lower(d2);
    return d2;
}

/*
 * Helper function for string processing.
 */
inline wstring trim_punc(const wstring &in) {
    wstring d2(in);
    boost::trim_if(d2, [](wchar_t c) { return iswpunct(c); });
    return d2;
}

/*
 * Helper function for string processing.
 */
wstring clean_whitespace(const wstring &input) {

    boost::wregex re(L"\n(\n|[[:space:]])+");
    boost::wregex re2(L"\\\\n(\\\\n|[[:space:]])+");
    wstring result = boost::regex_replace(input, re, L"\n");
    wstring result2 = boost::regex_replace(result, re2, L"\\\\n");
    result2 = boost::trim_copy(result2);
    return result2;
}

/*
 * Split a string into a vector of tokens (for split-search).
 */
vector<wstring> TesseractOCRTextDetection::get_tokens(const wstring &str) {
    vector<wstring> dt;
    wstringstream ss;
    wstring tmp;
    ss << str;
    for (size_t i; !ss.eof(); ++i) {
        ss >> tmp;
        dt.push_back(to_lowercase(trim_punc(tmp)));
    }
    return dt;
}

/*
 * Reads JSON Tag filter file.
 * Setup tags for split-string and regex filters.
 */
map<wstring, map<wstring, vector<wstring>>>
TesseractOCRTextDetection::parse_json(const MPFJob &job, const string &jsonfile_path, MPFDetectionError &job_status) {
    map<wstring, map<wstring, vector<wstring>>> json_kvs;
    ifstream ifs(jsonfile_path);
    if (!ifs.is_open()) {
        LOG4CXX_ERROR(hw_logger_, "[" + job.job_name + "] Error reading JSON file at " + jsonfile_path);
        job_status = MPF_COULD_NOT_READ_DATAFILE;
        return json_kvs;
    }
    string j;
    stringstream buffer2;
    wstring x;
    buffer2 << ifs.rdbuf();
    j = buffer2.str();
    x = boost::locale::conv::utf_to_utf<wchar_t>(j);
    JSONValue *value = JSON::Parse(x.c_str());
    if (value == NULL) {
        LOG4CXX_ERROR(hw_logger_,
                      "[" + job.job_name + "] JSON is corrupted. File location: " + jsonfile_path);
        job_status = MPF_COULD_NOT_READ_DATAFILE;
        return json_kvs;
    }
    JSONObject root;
    root = value->AsObject();


    map<wstring, vector<wstring>> json_kvs_string;
    map<wstring, vector<wstring>> json_kvs_regex;
    map<wstring, vector<wstring>> json_kvs_string_split;
    if (root.find(L"TAGS_STRING") != root.end() && root[L"TAGS_STRING"]->IsObject()) {
        LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] String tags found.");
        JSONValue *root2 = JSON::Parse(root[L"TAGS_STRING"]->Stringify().c_str());
        vector<wstring> keys = root2->ObjectKeys();
        vector<wstring>::iterator iter = keys.begin();
        JSONObject root3 = root2->AsObject();
        while (iter != keys.end()) {
            auto term = *iter;
            wstring term_temp(term);
            string term_str(term_temp.begin(), term_temp.end());
            if (!root3[term]->IsArray()) {
                LOG4CXX_ERROR(hw_logger_, "[" + job.job_name + "] Invalid JSON Array in STRING tags!");
                job_status = MPF_COULD_NOT_READ_DATAFILE;
                // There was a processing error, but continue checking the remaining terms.
                iter++;
                continue;
            }
            JSONArray array = root3[term]->AsArray();
            for (unsigned int i = 0; i < array.size(); i++) {
                wstring temp = array[i]->Stringify();
                temp = temp.substr(1, temp.size() - 2);
                temp = to_lowercase(temp);
                json_kvs_string[term].push_back(temp);
            }
            iter++;
        }
    }


    if (root.find(L"TAGS_BY_KEYWORD") != root.end() && root[L"TAGS_BY_KEYWORD"]->IsObject()) {
        LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Split tags found.");
        JSONValue *root2 = JSON::Parse(root[L"TAGS_BY_KEYWORD"]->Stringify().c_str());
        vector<wstring> keys = root2->ObjectKeys();
        vector<wstring>::iterator iter = keys.begin();
        JSONObject root3 = root2->AsObject();
        while (iter != keys.end()) {
            auto term = *iter;
            wstring term_temp(term);
            string term_str(term_temp.begin(), term_temp.end());
            if (!root3[term]->IsArray()) {
                LOG4CXX_ERROR(hw_logger_,
                              "[" + job.job_name + "] Invalid JSON Array in TAGS_BY_KEYWORD!");
                job_status = MPF_COULD_NOT_READ_DATAFILE;
                // There was a processing error, but continue checking the remaining terms.
                iter++;
                continue;
            }
            JSONArray array = root3[term]->AsArray();
            for (unsigned int i = 0; i < array.size(); i++) {
                wstring temp = array[i]->Stringify();
                temp = temp.substr(1, temp.size() - 2);
                temp = to_lowercase(temp);
                json_kvs_string_split[term].push_back(temp);
            }
            iter++;
        }
    } else {
        LOG4CXX_WARN(hw_logger_, "[" + job.job_name + "] TAGS_BY_KEYWORD NOT FOUND.");
    }

    // REGEX STUFF
    if (root.find(L"TAGS_BY_REGEX") != root.end() && root[L"TAGS_BY_REGEX"]->IsObject()) {
        LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Regex tags found.");
        JSONValue *root2 = JSON::Parse(root[L"TAGS_BY_REGEX"]->Stringify().c_str());
        vector<wstring> keys = root2->ObjectKeys();
        vector<wstring>::iterator iter = keys.begin();
        JSONObject root3 = root2->AsObject();
        while (iter != keys.end()) {
            auto term = *iter;
            wstring term_temp(term);
            string term_str(term_temp.begin(), term_temp.end());
            if (!root3[term]->IsArray()) {
                LOG4CXX_ERROR(hw_logger_, "[" + job.job_name + "] Invalid JSON Array in TAGS_BY_REGEX!");
                job_status = MPF_COULD_NOT_READ_DATAFILE;
                // There was a processing error, but continue checking the remaining terms.
                iter++;
                continue;
            }
            JSONArray array = root3[term]->AsArray();
            for (unsigned int i = 0; i < array.size(); i++) {
                wstring temp = array[i]->Stringify();
                temp = temp.substr(1, temp.size() - 2);
                temp = fix_regex(temp);
                temp = to_lowercase(temp);
                json_kvs_regex[term].push_back(temp);
            }
            iter++;
        }
    } else {
        LOG4CXX_WARN(hw_logger_, "[" + job.job_name + "] TAGS_BY_REGEX NOT FOUND.");
    }


    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] successfully read JSON.");
    json_kvs[L"TAGS_STRING"] = json_kvs_string;
    json_kvs[L"TAGS_STRING_SPLIT"] = json_kvs_string_split;
    json_kvs[L"TAGS_REGEX"] = json_kvs_regex;
    return json_kvs;
}

/*
 * Verify that two strings are identical (ignore letter case).
 */
bool TesseractOCRTextDetection::comp_strcmp(const wstring &strHaystack, const wstring &strNeedle) {
    auto it = search(
            strHaystack.begin(), strHaystack.end(),
            strNeedle.begin(), strNeedle.end(),
            [](char ch1, char ch2) { return toupper(ch1) == toupper(ch2); }
    );
    return (it != strHaystack.end());
}

/*
 * Check if detection string contains regstr pattern.
 */
bool TesseractOCRTextDetection::comp_regex(const MPFImageJob &job, const wstring &detection, const wstring &regstr,
                                           MPFDetectionError &job_status) {
    bool found = false;
    try {
        boost::wregex reg_matcher(regstr, boost::regex_constants::extended);
        boost::wsmatch m;
        if (boost::regex_search(detection, m, reg_matcher)) {
            found = true;
        }
    } catch (const boost::regex_error &e) {
        stringstream ss;
        ss << "[" + job.job_name + "] regex_error caught: " << parse_regex_code(e.code()) << ": " << e.what() << '\n';
        LOG4CXX_ERROR(hw_logger_, ss.str());
        job_status = MPF_COULD_NOT_READ_DATAFILE;
    }
    return found;
}

/*
 * Regex error handling.
 */
string TesseractOCRTextDetection::parse_regex_code(boost::regex_constants::error_type etype) {
    switch (etype) {
        case boost::regex_constants::error_collate:
            return "error_collate: invalid collating element request";
        case boost::regex_constants::error_ctype:
            return "error_ctype: invalid character class";
        case boost::regex_constants::error_escape:
            return "error_escape: invalid escape character or trailing escape";
        case boost::regex_constants::error_backref:
            return "error_backref: invalid back reference";
        case boost::regex_constants::error_brack:
            return "error_brack: mismatched bracket([ or ])";
        case boost::regex_constants::error_paren:
            return "error_paren: mismatched parentheses(( or ))";
        case boost::regex_constants::error_brace:
            return "error_brace: mismatched brace({ or })";
        case boost::regex_constants::error_badbrace:
            return "error_badbrace: invalid range inside a { }";
        case boost::regex_constants::error_range:
            return "erro_range: invalid character range(e.g., [z-a])";
        case boost::regex_constants::error_space:
            return "error_space: insufficient memory to handle this regular expression";
        case boost::regex_constants::error_badrepeat:
            return "error_badrepeat: a repetition character (*, ?, +, or {) was not preceded by a valid regular expression";
        case boost::regex_constants::error_complexity:
            return "error_complexity: the requested match is too complex";
        case boost::regex_constants::error_stack:
            return "error_stack: insufficient memory to evaluate a match";
        default:
            return "";
    }
}

/*
 * Generate a random alphanumeric string of specified length.
 * Used to generate image placeholders.
 */
string random_string(size_t length) {
    static auto &chrs = "0123456789"
                        "abcdefghijklmnopqrstuvwxyz"
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    thread_local static mt19937 rg{random_device{}()};
    thread_local static uniform_int_distribution<string::size_type> pick(0, sizeof(chrs) - 2);

    string s;

    s.reserve(length);

    while (length--) {
        s += chrs[pick(rg)];
    }
    ostringstream stp;
    stp << ::getpid();
    return s + stp.str();
}

/*
 * Preprocess image before running OSD and OCR.
 */
bool TesseractOCRTextDetection::preprocess_image(const MPFImageJob &job, cv::Mat &image_data,
                                                 const TesseractOCRTextDetection::OCR_filter_settings &ocr_fset,
                                                 MPFDetectionError &job_status) {
    if (image_data.empty()) {
        LOG4CXX_ERROR(hw_logger_,
                      "[" + job.job_name + "] Could not open transformed image and will not return detections");
        job_status = MPF_IMAGE_READ_ERROR;
        return false;
    } else {
        LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Transformed image opened.");
    }
    // Image preprocessig to improve text extraction results.

    // Image thresholding.
    if (ocr_fset.enable_otsu_thrs || ocr_fset.enable_adaptive_thrs) {
        cv::cvtColor(image_data, image_data, cv::COLOR_BGR2GRAY);
    }
    if (ocr_fset.enable_adaptive_thrs) {
        // Pixel blocksize ranges 5-51 worked for adaptive threshold.
        cv::adaptiveThreshold(image_data, image_data, 255, cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY,
                              ocr_fset.adaptive_thrs_pixel, ocr_fset.adaptive_thrs_c);
    } else if (ocr_fset.enable_otsu_thrs) {
        double thresh_val = cv::threshold(image_data, image_data, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    }

    // Rescale and sharpen image (larger images improve detection results).

    if (ocr_fset.enable_rescale) {
        cv::resize(image_data, image_data, cv::Size(), ocr_fset.scale, ocr_fset.scale);
    }
    if (ocr_fset.enable_sharpen) {
        sharpen(image_data, ocr_fset.sharpen);
    }

    // Image inversion.
    if (ocr_fset.invert) {
        double min, max;
        cv::Mat tmp_imb(image_data.size(), image_data.type());
        cv::minMaxLoc(image_data, &min, &max);
        tmp_imb.setTo(cv::Scalar::all(max));
        cv::subtract(tmp_imb, image_data, image_data);
    }

    return true;
}

/*
 * Run tesseract ocr on refined image.
 */
bool TesseractOCRTextDetection::get_tesseract_detections(const MPFImageJob &job,
                                                         vector<TesseractOCRTextDetection::OCR_output> &detections_by_lang,
                                                         cv::Mat &imi,
                                                         const TesseractOCRTextDetection::OCR_filter_settings &ocr_fset,
                                                         MPFDetectionError &job_status,
                                                         const string &tessdata_script_dir) {
    vector<string> results;
    boost::algorithm::split(results, job.job_name, boost::algorithm::is_any_of(" :"));
    vector<string> lang_tracks;
    boost::algorithm::split(lang_tracks, ocr_fset.tesseract_lang, boost::algorithm::is_any_of(","));
    for (string lang: lang_tracks) {
        // Process each language specified by user.
        lang = boost::trim_copy(lang);

        vector<string> languages;
        boost::algorithm::split(languages, lang, boost::algorithm::is_any_of("+"));

        // Trim whitespace.
        for (string &c_lang : languages) {
            c_lang = boost::trim_copy(c_lang);
        }

        lang = boost::algorithm::join(languages, "+");
        pair<int, string> tess_api_key = make_pair(ocr_fset.oem, lang);
        LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Running Tesseract with specified language: " + lang);

        if (tess_api_map.find(tess_api_key) == tess_api_map.end()) {

            // Confirm each language model is present in tessdata or shared model directory.
            // Language models that run together must be present in the same directory.
            string tessdata_dir = "";
            if (tessdata_script_dir == "") {
                // Left blank when OSD is not run or scripts not found and reverted to default language.
                // Check default language model is present.
                tessdata_dir = return_valid_tessdir(job, lang, ocr_fset.model_dir);
            } else {
                tessdata_dir = tessdata_script_dir;
            }

            if (tessdata_dir == "") {
                LOG4CXX_ERROR(hw_logger_, "[" + job.job_name + "] Tesseract language models not found. Please add the " +
                                          "associated *.traineddata files to your tessdata directory " +
                                          "($MPF_HOME/plugins/TesseractOCRTextDetection/tessdata) " +
                                          "or shared models directory " +
                                          "($MPF_HOME/share/models/TesseractOCRTextDetection/tessdata).");
                job_status = MPF_COULD_NOT_OPEN_DATAFILE;
                return false;
            }

            tesseract::TessBaseAPI *tess_api = new tesseract::TessBaseAPI();
            int init_rc = tess_api->Init(tessdata_dir.c_str(), lang.c_str(), (tesseract::OcrEngineMode) ocr_fset.oem);

            if (init_rc != 0) {
                LOG4CXX_ERROR(hw_logger_, "[" + job.job_name + "] Failed to initialize Tesseract! Error code: " +
                                          to_string(init_rc));
                job_status = MPF_DETECTION_NOT_INITIALIZED;
                return false;
            }

            tess_api_map[tess_api_key] = tess_api;
        }

        tesseract::TessBaseAPI *tess_api = tess_api_map[tess_api_key];
        tess_api->SetPageSegMode((tesseract::PageSegMode) ocr_fset.psm);
        tess_api->SetImage(imi.data, imi.cols, imi.rows, imi.channels(), static_cast<int>(imi.step));
        unique_ptr<char[]> text{tess_api->GetUTF8Text()};
        double confidence = tess_api->MeanTextConf();

        // Free up recognition results and any stored image data.
        tess_api->Clear();

        string result = text.get();

        LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Tesseract run successful.");
        wstring t_detection = boost::locale::conv::utf_to_utf<wchar_t>(result);

        TesseractOCRTextDetection::OCR_output output_ocr = {confidence, lang, t_detection};
        detections_by_lang.push_back(output_ocr);
    }

    // If max_text_tracks is set, filter out to return only the top specified tracks.
    if (ocr_fset.max_text_tracks > 0) {
        sort(detections_by_lang.begin(), detections_by_lang.end(), greater<TesseractOCRTextDetection::OCR_output>());
        detections_by_lang.resize(ocr_fset.max_text_tracks);
    }
    return true;
}

string TesseractOCRTextDetection::return_valid_tessdir(const MPFImageJob &job, const string &lang_str,
                                                       const string &directory) {

    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Checking tessdata models in " + directory + ".");

    if (check_tess_model_directory(job, lang_str, directory)) {
        return directory;
    }

    string run_dir = GetRunDirectory();
    if (run_dir.empty()) {
        run_dir = ".";
    }

    string local_plugin_directory = run_dir + "/TesseractOCRTextDetection/tessdata";
    LOG4CXX_DEBUG(hw_logger_,
                  "[" + job.job_name + "] Not all models found in " + directory + ". Checking local plugin directory "
                  + local_plugin_directory + ".");

    if (check_tess_model_directory(job, lang_str, local_plugin_directory)) {
        return local_plugin_directory;
    }

    LOG4CXX_WARN(hw_logger_, "[" + job.job_name + "] Some Tessdata models were not found, or are not co-located." +
                             " Please ensure that all of the following models exist in the same directory, either " +
                             directory + " or " +
                             local_plugin_directory + ": " + lang_str + " .");
    return "";
}

bool TesseractOCRTextDetection::check_tess_model_directory(const MPFImageJob &job, const string &lang_str,
                                                           const string &directory) {

    vector<string> langs;
    boost::split(langs, lang_str, boost::is_any_of(",+"));

    for (string lang : langs) {
        boost::trim(lang);
        if (!boost::filesystem::exists(directory + "/" + lang + ".traineddata")) {
            LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Tessdata file " + lang + ".traineddata does not exist in "
                                      + directory + ".");
            return false;
        }
        LOG4CXX_DEBUG(hw_logger_,
                      "[" + job.job_name + "] Tessdata file " + lang + ".traineddata found in  " + directory + ".");
    }
    LOG4CXX_INFO(hw_logger_,
                 "[" + job.job_name + "] All tessdata models found in " + directory + ": " + lang_str + ".");
    return true;
}

void TesseractOCRTextDetection::get_OSD(OSResults &results, cv::Mat &imi, const MPFImageJob &job,
                                        TesseractOCRTextDetection::OCR_filter_settings &ocr_fset,
                                        Properties &detection_properties, MPFDetectionError &job_status,
                                        string &tessdata_script_dir) {
    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Running Tesseract OSD.");

    int oem = 3;
    pair<int, string> tess_api_key = make_pair(oem, "osd");

    if (tess_api_map.find(tess_api_key) == tess_api_map.end()) {
        LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Loading OSD model.");

        string tessdata_dir = return_valid_tessdir(job, "osd", ocr_fset.model_dir);
        if (tessdata_dir == "") {
            LOG4CXX_ERROR(hw_logger_, "[" + job.job_name + "] OSD model not found!");
            job_status = MPF_COULD_NOT_OPEN_DATAFILE;
            return;
        }

        tesseract::TessBaseAPI *tess_api = new tesseract::TessBaseAPI();
        int init_rc = tess_api->Init(tessdata_dir.c_str(), "osd", (tesseract::OcrEngineMode) oem);

        if (init_rc != 0) {
            LOG4CXX_ERROR(hw_logger_,
                          "[" + job.job_name + "] Failed to initialize Tesseract! Error code: " + to_string(init_rc));
            job_status = MPF_DETECTION_NOT_INITIALIZED;
            return;
        }

        tess_api_map[tess_api_key] = tess_api;

        LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] OSD model ready.");
    }

    tesseract::TessBaseAPI *tess_api = tess_api_map[tess_api_key];
    tess_api->SetPageSegMode(tesseract::PSM_AUTO_OSD);
    tess_api->SetImage(imi.data, imi.cols, imi.rows, imi.channels(), static_cast<int>(imi.step));
    tess_api->DetectOS(&results);

    // Free up recognition results and any stored image data.
    tess_api->Clear();

    int best_ori = results.best_result.orientation_id;
    int best_id = results.best_result.script_id;
    int candidates = 0;
    double best_score = results.scripts_na[best_ori][best_id];
    int rotation = 0;

    if (ocr_fset.min_orientation_confidence <= results.best_result.oconfidence) {
        switch (results.best_result.orientation_id) {
            case 0:
                // Do not rotate.
                break;
            case 1:
                // 270 degree clockwise rotation.
                // 90 degree counterclockwise rotation.
                cv::rotate(imi, imi, cv::ROTATE_90_COUNTERCLOCKWISE);
                rotation = 90;
                break;
            case 2:
                // 180 degree rotation.
                cv::rotate(imi, imi, cv::ROTATE_180);
                rotation = 180;
                break;
            case 3:
                // 90 degree clockwise rotation.
                // 270 degree counterclockwise rotation.
                cv::rotate(imi, imi, cv::ROTATE_90_CLOCKWISE);
                rotation = 270;
                break;
            default:
                break;
        }
    }

    // Store OSD results.
    detection_properties["PRIMARY_SCRIPT"] = results.unicharset->get_script_from_script_id(
            results.best_result.script_id);
    detection_properties["PRIMARY_SCRIPT_CONFIDENCE"] = to_string(results.best_result.sconfidence);
    detection_properties["PRIMARY_SCRIPT_SCORE"] = to_string(best_score);
    detection_properties["ROTATION"] = to_string(rotation);
    detection_properties["ROTATION_CONFIDENCE"] = to_string(results.best_result.oconfidence);

    int max_scripts = ocr_fset.max_scripts;
    if (detection_properties["PRIMARY_SCRIPT"] == "Common") {
        // When "Common" is detected, swap over to secondary scripts for consideration.
        // Common script is automatically excluded from model selection, but still reported in the results as the top
        // script.
        if (max_scripts > 0) {
            max_scripts = max_scripts + 1;
        }
    }

    if (ocr_fset.min_script_confidence <= results.best_result.sconfidence && ocr_fset.min_script_score <= best_score) {

        TesseractOCRTextDetection::OSD_script best_script = {best_id, best_score};
        vector<TesseractOCRTextDetection::OSD_script> script_list;

        if (max_scripts != 1) {
            // Max number of scripts in ICU + "NULL" + Japanese and Korean + Fraktur
            const int kMaxNumberOfScripts = 116 + 1 + 2 + 1;
            double score_cutoff = best_score * ocr_fset.min_secondary_script_thrs;
            if (score_cutoff < ocr_fset.min_script_score) {
                score_cutoff = ocr_fset.min_script_score;
            }
            // Skip index 0 to ignore the "Common" script.
            for (int i = 1; i < kMaxNumberOfScripts; ++i) {
                double score = results.scripts_na[best_ori][i];
                if (i != best_id && score >= score_cutoff) {
                    TesseractOCRTextDetection::OSD_script script_out;
                    script_out.id = i;
                    script_out.score = score;
                    script_list.push_back(script_out);
                    candidates += 1;
                }

            }
            sort(script_list.begin(), script_list.end(), greater<TesseractOCRTextDetection::OSD_script>());
            // Limit number of accepted scripts if user set max_scripts to 2 or greater.
            // Unlimited number when users sets max_scripts to 0 or below.
            if (max_scripts > 1 && candidates > max_scripts - 1) {
                script_list.resize(max_scripts - 1);
            }
        }

        // Store OSD results.
        if (script_list.size() > 0) {

            vector<string> scripts;
            vector<string> scores;

            for (TesseractOCRTextDetection::OSD_script script : script_list) {
                scripts.push_back(results.unicharset->get_script_from_script_id(script.id));
                scores.push_back(to_string(script.score));
            }

            detection_properties["SECONDARY_SCRIPTS"] = boost::algorithm::join(scripts, ", ");
            detection_properties["SECONDARY_SCRIPT_SCORES"] = boost::algorithm::join(scores, ", ");
        }

        // Include best script result and move it to the front.
        script_list.push_back(best_script);
        rotate(script_list.rbegin(), script_list.rbegin() + 1, script_list.rend());

        bool added_japanese = false;
        bool added_korean = false;
        vector<string> script_results;
        for (TesseractOCRTextDetection::OSD_script script : script_list) {
            string script_type = results.unicharset->get_script_from_script_id(script.id);
            // For scripts that support vertical text, run both horizontal and vertical language models.
            if (script_type == "Han") {

                if (ocr_fset.combine_detected_scripts) {
                    script_results.push_back("script/HanS+script/HanT+script/HanS_vert+script/HanT_vert");
                } else {
                    script_results.push_back("script/HanS+script/HanT,script/HanS_vert+script/HanT_vert");
                }

            } else if (script_type == "Korean" || script_type == "Hangul") {
                if (added_korean) {
                    continue;
                }
                // Don't add the same language multiple times.
                added_korean = true;
                if (ocr_fset.combine_detected_scripts) {
                    script_results.push_back("script/Hangul+script/Hangul_vert");
                } else {
                    script_results.push_back("script/Hangul,script/Hangul_vert");
                }

            } else if ((script_type == "Japanese" || script_type == "Hiragana" || script_type == "Katakana")) {
                if (added_japanese) {
                    continue;
                }
                // Don't add the same language multiple times.
                added_japanese = true;
                if (ocr_fset.combine_detected_scripts) {
                    script_results.push_back("script/Japanese+script/Japanese_vert");
                } else {
                    script_results.push_back("script/Japanese,script/Japanese_vert");
                }
            } else if (script_type == "Common") {
                continue;
            } else {
                script_results.push_back("script/" + script_type);
            }
        }

        string lang_str;
        if (ocr_fset.combine_detected_scripts) {
            lang_str = boost::algorithm::join(script_results, "+");
        } else {
            lang_str = boost::algorithm::join(script_results, ",");
        }

        // Check if selected models are present in either models or tessdata directory.
        // All language models must be present in one directory.
        // If scripts are not found, revert to default.
        // If scripts are found, set return_valid_tessdir so that get_tesseract_detections will skip searching for models.
        tessdata_script_dir = TesseractOCRTextDetection::return_valid_tessdir(job, lang_str, ocr_fset.model_dir);
        if (tessdata_script_dir == "") {
            LOG4CXX_WARN(hw_logger_, "[" + job.job_name + "] Script models not found in model and tessdata directories,"
                                     + " reverting to default language setting: " + ocr_fset.tesseract_lang);
        } else {
            ocr_fset.tesseract_lang = lang_str;
        }
    }

}

template<typename T, typename U>
T &replace(
        T &str,
        const U &from,
        const U &to) {
    size_t pos;
    size_t offset = 0;
    const size_t increment = to.size();

    while ((pos = str.find(from, offset)) != T::npos) {
        str.replace(pos, from.size(), to);
        offset = pos + increment;
    }

    return str;
}

wstring TesseractOCRTextDetection::fix_regex(wstring inreg) {
    for (auto const &x : reg_table) {
        replace(inreg, x.first, x.second);
    }
    return inreg;
}

bool is_only_ascii_whitespace(const wstring &str) {
    auto it = str.begin();
    do {
        if (it == str.end()) {
            return true;
        }
    } while (*it >= 0 && *it <= 0x7f && iswspace(*(it++)));
    // One of these conditions will be optimized away by the compiler,
    // which one depends on whether char is signed or not
    return false;
}


/*
 * Performs regex-tagging of ocr text detection.
 */
set<wstring> TesseractOCRTextDetection::search_regex(const MPFImageJob &job, const wstring &ocr_detections,
                                                     const map<wstring, vector<wstring>> &json_kvs_regex,
                                                     MPFDetectionError &job_status) {
    wstring found_tags_regex = L"";
    set<wstring> found_keys_regex;
    if (json_kvs_regex.size() == 0) {
        return found_keys_regex;
    }

    for (const auto &kv : json_kvs_regex) {
        auto key = kv.first;
        auto values = kv.second;
        for (auto value : values) {
            if (comp_regex(job, ocr_detections, value, job_status)) {
                found_keys_regex.insert(key);
                break;
            }
        }
    }
    int num_found = 0;
    for (auto tags : found_keys_regex) {
        found_tags_regex += tags + L", ";
        num_found++;
    }
    if (num_found > 0) {
        found_tags_regex = found_tags_regex.substr(0, found_tags_regex.size() - 2);
    }
    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Done searching for regex tags, found: " + to_string(num_found));
    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Found regex tags are: " +
                              boost::locale::conv::utf_to_utf<char>(found_tags_regex));

    return found_keys_regex;
}

bool TesseractOCRTextDetection::token_comparison(const wstring &token, const wstring &value) {
    static locale loc = locale();
    int result = use_facet<boost::locale::collator<wchar_t>>(loc).compare(boost::locale::collator_base::primary, token,
                                                                          value);
    return result == 0;
}

/*
 * Performs split-string-tagging of ocr text detection.
 */
set<wstring> TesseractOCRTextDetection::search_string_split(const MPFImageJob &job, const vector<wstring> &tokenized,
                                                            const map<wstring, vector<wstring>> &json_kvs_string) {
    wstring found_tags_string = L"";
    set<wstring> found_keys_string;
    if (json_kvs_string.size() == 0) {
        return found_keys_string;
    }
    boost::wregex rgx(L"\\s+");
    for (const auto &kv : json_kvs_string) {
        auto key = kv.first;
        auto values = kv.second;
        bool breaker = false;
        for (auto value : values) {
            vector<wstring> tag_tokens;
            boost::split_regex(tag_tokens, value, rgx);

            if (tag_tokens.size() == 1) {
                // For single keyword matching.
                for (const auto &token : tokenized) {
                    if (token_comparison(token, value)) {
                        found_keys_string.insert(key);
                        breaker = true;
                        break;
                    }
                }
                if (breaker) break;
            } else {
                // For multi-length keyphrase matching.
                // Check that each keyword in the phrase matches in order
                // with words in the extracted text.
                int word_id = 0;
                for (const auto &token : tokenized) {
                    if (token_comparison(token, tag_tokens[word_id])) {
                        // Current word matched part of phrase.
                        word_id++;
                        // Check if entire phrase has been matched.
                        if (word_id == tag_tokens.size()) {
                            // Entire phrase matched correctly.
                            found_keys_string.insert(key);
                            breaker = true;
                            break;
                        }
                    } else if (word_id > 0) {
                        // If a phrase match fails midway.
                        // Restart phrase check and compare word with starting
                        // keyword in that phrase,
                        if (token_comparison(token, tag_tokens[0])) {
                            // Current word matches with start of phrase.
                            // Proceed to check with next keyword in phrase.
                            word_id = 1;
                        } else {
                            // Current word does not match start of phrase.
                            // Compare next word with starting keyword in phrase.
                            word_id = 0;
                        }
                    }
                }

                if (breaker) break;
            }
        }
    }
    int num_found = 0;
    for (auto tags : found_keys_string) {
        found_tags_string += tags + L", ";
        num_found++;
    }
    if (num_found > 0) {
        found_tags_string = found_tags_string.substr(0, found_tags_string.size() - 2);
    }
    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Done searching for string tags, found: " + to_string(num_found));
    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Found string tags are: " +
                              boost::locale::conv::utf_to_utf<char>(found_tags_string));
    return found_keys_string;
}

/*
 * Performs full-string tagging of ocr text detection.
 */
set<wstring> TesseractOCRTextDetection::search_string(const MPFImageJob &job, const wstring &ocr_detections,
                                                      const map<wstring, vector<wstring>> &json_kvs_string) {
    wstring found_tags_string = L"";
    set<wstring> found_keys_string;
    if (json_kvs_string.size() == 0) {
        return found_keys_string;
    }
    for (const auto &kv : json_kvs_string) {
        auto key = kv.first;
        auto values = kv.second;

        for (auto value : values) {
            if (comp_strcmp(ocr_detections, value)) {
                found_keys_string.insert(key);
                break;
            }
        }
    }
    int num_found = 0;
    for (auto tags : found_keys_string) {
        found_tags_string += tags + L", ";
        num_found++;
    }
    if (num_found > 0) {
        found_tags_string = found_tags_string.substr(0, found_tags_string.size() - 2);
    }
    LOG4CXX_DEBUG(hw_logger_,
                  "[" + job.job_name + "] Done searching for full string tags, found: " + to_string(num_found));
    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Found full string tags are: " +
                              boost::locale::conv::utf_to_utf<char>(found_tags_string));
    return found_keys_string;
}


void TesseractOCRTextDetection::load_tags_json(const MPFJob &job, MPFDetectionError &job_status,
                                               map<wstring, vector<wstring>> &json_kvs_string,
                                               map<wstring, vector<wstring>> &json_kvs_string_split,
                                               map<wstring, vector<wstring>> &json_kvs_regex) {

    string run_dir = GetRunDirectory();
    if (run_dir.empty()) {
        run_dir = ".";
    }
    string plugin_path = run_dir + "/TesseractOCRTextDetection";
    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Running from directory " + plugin_path);

    string jsonfile_path = DetectionComponentUtils::GetProperty<string>(job.job_properties, "TAGGING_FILE",
                                                                        "text-tags.json");
    if (jsonfile_path.find('$') != string::npos || jsonfile_path.find('/') != string::npos) {
        string new_jsonfile_name = "";
        Utils::expandFileName(jsonfile_path, new_jsonfile_name);
        jsonfile_path = new_jsonfile_name;
    } else {
        jsonfile_path = plugin_path + "/config/" + jsonfile_path;
    }

    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] About to read JSON from: " + jsonfile_path);
    auto json_kvs_full = parse_json(job, jsonfile_path, job_status);
    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Read JSON");

    json_kvs_string = json_kvs_full[L"TAGS_STRING"];
    json_kvs_string_split = json_kvs_full[L"TAGS_STRING_SPLIT"];
    json_kvs_regex = json_kvs_full[L"TAGS_REGEX"];
}

void
TesseractOCRTextDetection::load_settings(const MPFJob &job, TesseractOCRTextDetection::OCR_filter_settings &ocr_fset) {
    // Load in settings specified from job_properties and default configuration.
    ocr_fset.sharpen = DetectionComponentUtils::GetProperty<double>(job.job_properties,"SHARPEN", default_ocr_fset.sharpen);
    ocr_fset.invert = DetectionComponentUtils::GetProperty<bool>(job.job_properties,"INVERT", default_ocr_fset.invert);
    
    // String filtering
    ocr_fset.threshold_check = DetectionComponentUtils::GetProperty<bool>(job.job_properties,"THRS_FILTER", default_ocr_fset.threshold_check);
    ocr_fset.hist_check = DetectionComponentUtils::GetProperty<bool>(job.job_properties,"HIST_FILTER", default_ocr_fset.hist_check);
    ocr_fset.num_only_ok = DetectionComponentUtils::GetProperty<bool>(job.job_properties,"NUM_ONLY", default_ocr_fset.num_only_ok);
    ocr_fset.min_word_len = DetectionComponentUtils::GetProperty<int>(job.job_properties,"MIN_WORD_LEN", default_ocr_fset.min_word_len);
    ocr_fset.hist_min_char = DetectionComponentUtils::GetProperty<int>(job.job_properties,"MIN_HIST_SIZE", default_ocr_fset.hist_min_char);
    ocr_fset.excess_eng_symbols = DetectionComponentUtils::GetProperty<float>(job.job_properties,"MAX_ENG_PNCT", default_ocr_fset.excess_eng_symbols);
    ocr_fset.excess_non_eng_symbols = DetectionComponentUtils::GetProperty<float>(job.job_properties,"MAX_FRN_CHAR", default_ocr_fset.excess_non_eng_symbols);
    ocr_fset.vowel_min = DetectionComponentUtils::GetProperty<float>(job.job_properties,"VOWEL_MIN", default_ocr_fset.vowel_min);
    ocr_fset.vowel_max = DetectionComponentUtils::GetProperty<float>(job.job_properties,"VOWEL_MAX", default_ocr_fset.vowel_max);
    ocr_fset.correl_limit = DetectionComponentUtils::GetProperty<float>(job.job_properties,"MIN_HIST_SCORE", default_ocr_fset.correl_limit);
    
    // Image preprocessing
    ocr_fset.scale = DetectionComponentUtils::GetProperty<double>(job.job_properties,"SCALE", default_ocr_fset.scale);
    ocr_fset.adaptive_thrs_c = DetectionComponentUtils::GetProperty<double>(job.job_properties,"ADAPTIVE_THRS_CONSTANT", default_ocr_fset.adaptive_thrs_c);
    ocr_fset.adaptive_thrs_pixel = DetectionComponentUtils::GetProperty<int>(job.job_properties,"ADAPTIVE_THRS_BLOCKSIZE", default_ocr_fset.adaptive_thrs_pixel);
    ocr_fset.enable_adaptive_thrs = DetectionComponentUtils::GetProperty<bool>(job.job_properties,"ENABLE_ADAPTIVE_THRS", default_ocr_fset.enable_adaptive_thrs);
    ocr_fset.enable_otsu_thrs = DetectionComponentUtils::GetProperty<bool>(job.job_properties,"ENABLE_OTSU_THRS", default_ocr_fset.enable_otsu_thrs);
    ocr_fset.enable_sharpen = DetectionComponentUtils::GetProperty<bool>(job.job_properties,"ENABLE_SHARPEN", default_ocr_fset.enable_sharpen);
    ocr_fset.enable_rescale = DetectionComponentUtils::GetProperty<bool>(job.job_properties,"ENABLE_RESCALE", default_ocr_fset.enable_rescale);
    ocr_fset.tesseract_lang  = DetectionComponentUtils::GetProperty<std::string>(job.job_properties,"TESSERACT_LANGUAGE", default_ocr_fset.tesseract_lang);
    ocr_fset.psm = DetectionComponentUtils::GetProperty<int>(job.job_properties,"TESSERACT_PSM", default_ocr_fset.psm);
    ocr_fset.oem = DetectionComponentUtils::GetProperty<int>(job.job_properties,"TESSERACT_OEM", default_ocr_fset.oem);
    
    // OSD settings
    ocr_fset.enable_osd = DetectionComponentUtils::GetProperty<bool>(job.job_properties,"ENABLE_OSD_AUTOMATION", default_ocr_fset.enable_osd);
    ocr_fset.combine_detected_scripts = DetectionComponentUtils::GetProperty<bool>(job.job_properties,"COMBINE_OSD_SCRIPTS", default_ocr_fset.combine_detected_scripts);
    ocr_fset.min_orientation_confidence = DetectionComponentUtils::GetProperty<double>(job.job_properties,"MIN_OSD_ROTATION_CONFIDENCE", default_ocr_fset.min_orientation_confidence);
    ocr_fset.min_script_confidence = DetectionComponentUtils::GetProperty<double>(job.job_properties,"MIN_OSD_PRIMARY_SCRIPT_CONFIDENCE", default_ocr_fset.min_script_confidence);
    ocr_fset.min_script_score = DetectionComponentUtils::GetProperty<double>(job.job_properties,"MIN_OSD_SCRIPT_SCORE", default_ocr_fset.min_script_score);
    ocr_fset.max_scripts = DetectionComponentUtils::GetProperty<int>(job.job_properties,"MAX_OSD_SCRIPTS", default_ocr_fset.max_scripts);
    ocr_fset.min_secondary_script_thrs = DetectionComponentUtils::GetProperty<double>(job.job_properties,"MIN_OSD_SECONDARY_SCRIPT_THRESHOLD", default_ocr_fset.min_secondary_script_thrs);

    ocr_fset.max_text_tracks = DetectionComponentUtils::GetProperty<int>(job.job_properties,"MAX_TEXT_TRACKS", default_ocr_fset.max_text_tracks);

    // Tessdata setup
    ocr_fset.model_dir =  DetectionComponentUtils::GetProperty<std::string>(job.job_properties, "MODELS_DIR_PATH", default_ocr_fset.model_dir);
    if (ocr_fset.model_dir != "") {
        ocr_fset.model_dir = ocr_fset.model_dir + "/TesseractOCRTextDetection/tessdata";
    } else {
        string run_dir = GetRunDirectory();
        if (run_dir.empty()) {
            run_dir = ".";
        }
        string plugin_path = run_dir + "/TesseractOCRTextDetection";
        // If not specified, set model dir to local plugin dir.
        ocr_fset.model_dir = plugin_path + "/tessdata";
    }
    Utils::expandFileName(ocr_fset.model_dir, ocr_fset.model_dir);
}

// Tag results and store into track detection properties.
bool TesseractOCRTextDetection::process_text_tagging(Properties &detection_properties, const MPFImageJob &job,
                                                     TesseractOCRTextDetection::OCR_output &ocr_out,
                                                     MPFDetectionError &job_status,
                                                     const TesseractOCRTextDetection::OCR_filter_settings &ocr_fset,
                                                     const map<wstring, vector<wstring>> &json_kvs_regex,
                                                     const map<wstring, vector<wstring>> &json_kvs_string_split,
                                                     const map<wstring, vector<wstring>> &json_kvs_string,
                                                     int page_num) {

    string ocr_lang = ocr_out.language;
    wstring ocr_detections = ocr_out.text;
    ocr_detections = clean_whitespace(ocr_detections);
    if (ocr_fset.hist_check || ocr_fset.threshold_check) {
        ocr_detections = TesseractOCRTextDetection::check_string(ocr_detections, ocr_fset);
    }

    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Processing tags for Tesseract OCR output: ");
    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Tesseract OCR output was: " +
                              boost::locale::conv::utf_to_utf<char>(ocr_detections));

    if (is_only_ascii_whitespace(ocr_detections)) {
        LOG4CXX_WARN(hw_logger_, "[" + job.job_name + "] No text found in image!");
        return false;
    }

    auto tokenized = get_tokens(ocr_detections);
    wstring norm_detections = to_lowercase(ocr_detections);
    auto found_tags_regex = search_regex(job, norm_detections, json_kvs_regex, job_status);
    auto found_tags_string_split = search_string_split(job, tokenized, json_kvs_string_split);
    auto found_tags_string = search_string(job, norm_detections, json_kvs_string);

    for (auto tag: found_tags_string_split) {
        found_tags_string.insert(tag);
    }
    for (auto tag: found_tags_regex) {
        found_tags_string.insert(tag);
    }
    wstring tag_string = L"";
    int num_found = 0;
    for (auto tags : found_tags_string) {
        tag_string += tags + L", ";
        num_found++;
    }
    if (num_found > 0) {
        tag_string = tag_string.substr(0, tag_string.size() - 2);
    }

    detection_properties["TEXT_LANGUAGE"] = ocr_lang;
    detection_properties["TAGS"] = boost::locale::conv::utf_to_utf<char>(tag_string);
    detection_properties["TEXT"] = boost::locale::conv::utf_to_utf<char>(ocr_detections);

    if (page_num >= 0) {
        detection_properties["PAGE_NUM"] = to_string(page_num + 1);
    }
    return true;

}

MPFDetectionError
TesseractOCRTextDetection::GetDetections(const MPFImageJob &job, vector<MPFImageLocation> &locations) {
    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Processing \"" + job.data_uri + "\".");

    TesseractOCRTextDetection::OCR_filter_settings ocr_fset;
    load_settings(job, ocr_fset);

    MPFDetectionError job_status = MPF_DETECTION_SUCCESS;
    map<wstring, vector<wstring>> json_kvs_string;
    map<wstring, vector<wstring>> json_kvs_string_split;
    map<wstring, vector<wstring>> json_kvs_regex;
    load_tags_json(job, job_status, json_kvs_string, json_kvs_string_split, json_kvs_regex);

    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] About to run tesseract");
    vector<TesseractOCRTextDetection::OCR_output> ocr_outputs;

    MPFImageReader image_reader(job);
    cv::Mat image_data = image_reader.GetImage();
    cv::Size input_size = image_data.size();

    if (!preprocess_image(job, image_data, ocr_fset, job_status)) {
        return job_status;
    }

    MPFImageLocation osd_track_results(0, 0, input_size.width, input_size.height);
    string tessdata_script_dir = "";

    if (ocr_fset.psm == 0 || ocr_fset.enable_osd) {

        OSResults os_results;
        get_OSD(os_results, image_data, job, ocr_fset, osd_track_results.detection_properties, job_status,
                tessdata_script_dir);

        // When PSM is set to 0, there is no need to process any further.
        if (ocr_fset.psm == 0) {
            LOG4CXX_INFO(hw_logger_,
                         "[" + job.job_name + "] Processing complete. Found " + to_string(locations.size()) +
                         " tracks.");
            locations.push_back(osd_track_results);
            return job_status;
        }
    }

    if (!get_tesseract_detections(job, ocr_outputs, image_data, ocr_fset, job_status, tessdata_script_dir)) {
        LOG4CXX_ERROR(hw_logger_, "[" + job.job_name + "] Could not run tesseract!");
        return job_status;
    }

    for (auto ocr_out: ocr_outputs) {
        MPFImageLocation image_location(0, 0, input_size.width, input_size.height, ocr_out.confidence);
        // Copy over OSD detection results into OCR output track.
        image_location.detection_properties = osd_track_results.detection_properties;
        bool process_text = process_text_tagging(image_location.detection_properties, job, ocr_out, job_status,
                                                 ocr_fset,
                                                 json_kvs_regex, json_kvs_string_split, json_kvs_string);
        if (process_text) {
            locations.push_back(image_location);
        }
    }

    for (auto &location : locations) {
        image_reader.ReverseTransform(location);
    }

    LOG4CXX_INFO(hw_logger_,
                 "[" + job.job_name + "] Processing complete. Found " + to_string(locations.size()) + " tracks.");
    return job_status;
}

MPFDetectionError TesseractOCRTextDetection::GetDetections(const MPFGenericJob &job, vector<MPFGenericTrack> &tracks) {
    LOG4CXX_DEBUG(hw_logger_, "[" + job.job_name + "] Processing \"" + job.data_uri + "\".");

    TesseractOCRTextDetection::OCR_filter_settings ocr_fset;
    load_settings(job, ocr_fset);

    MPFDetectionError job_status = MPF_DETECTION_SUCCESS;
    map<wstring, vector<wstring>> json_kvs_string;
    map<wstring, vector<wstring>> json_kvs_string_split;
    map<wstring, vector<wstring>> json_kvs_regex;
    load_tags_json(job, job_status, json_kvs_string, json_kvs_string_split, json_kvs_regex);

    string temp_im_directory;
    vector<string> job_names;
    boost::split(job_names, job.job_name, boost::is_any_of(":"));
    string job_name = boost::ireplace_all_copy(job_names[0], "job", "");
    boost::trim(job_name);

    string run_dir = GetRunDirectory();
    if (run_dir.empty()) {
        run_dir = ".";
    }
    string plugin_path = run_dir + "/TesseractOCRTextDetection";
    temp_im_directory = plugin_path + "/tmp-" + job_name + "-" + random_string(20);

    if (boost::filesystem::exists(temp_im_directory)) {
        LOG4CXX_ERROR(hw_logger_, "[" + job.job_name + "] Unable to write temporary directory (already exists): " +
                                  temp_im_directory);
        return MPF_FILE_WRITE_ERROR;
    }

    vector<string> filelist;
    // Attempts to process generic document.
    // If read successfully, convert to images and store in a temporary directory.
    try {
        boost::filesystem::create_directory(temp_im_directory);
        Magick::ReadOptions options;
        options.density(Magick::Geometry(300, 300));
        options.depth(8);
        list<Magick::Image> images;
        readImages(&images, job.data_uri, options);
        size_t i;
        list<Magick::Image>::iterator image;
        for (image = images.begin(), i = 0; image != images.end(); image++, i++) {
            image->matte(false);
            image->backgroundColor(Magick::Color("WHITE"));
            string tiff_file = temp_im_directory + "/results_extracted" + to_string(i) + ".tiff";
            image->write(tiff_file);
            filelist.push_back(tiff_file);
        }

    } catch (Magick::Exception &error) {
        LOG4CXX_ERROR(hw_logger_, "[" + job.job_name + "] Error processing file " + job.data_uri + " .");
        LOG4CXX_ERROR(hw_logger_, "[" + job.job_name + "] Error: " + error.what());
        if (boost::filesystem::exists(temp_im_directory)) {
            boost::filesystem::remove_all(temp_im_directory);
        }
        return MPF_UNSUPPORTED_DATA_TYPE;
    }

    int page_num = 0;
    string default_lang = ocr_fset.tesseract_lang;
    for (string filename : filelist) {
        MPFImageJob c_job(job.job_name, filename, job.job_properties, job.media_properties);

        MPFImageReader image_reader(c_job);
        cv::Mat image_data = image_reader.GetImage();

        if (!preprocess_image(c_job, image_data, ocr_fset, job_status)) {
            return job_status;
        }

        MPFGenericTrack osd_track_results(-1);
        string tessdata_script_dir = "";
        if (ocr_fset.psm == 0 || ocr_fset.enable_osd) {
            OSResults os_results;
            // Reset to original specified language before processing OSD.
            ocr_fset.tesseract_lang = default_lang;
            get_OSD(os_results, image_data, c_job, ocr_fset, osd_track_results.detection_properties, job_status,
                    tessdata_script_dir);

            // When PSM is set to 0, there is no need to process any further.
            // Proceed to next page for OSD processing.
            if (ocr_fset.psm == 0) {
                osd_track_results.detection_properties["PAGE_NUM"] = to_string(page_num + 1);
                tracks.push_back(osd_track_results);
                page_num++;
                continue;
            }
        }

        vector<TesseractOCRTextDetection::OCR_output> ocr_outputs;
        if (!get_tesseract_detections(c_job, ocr_outputs, image_data, ocr_fset, job_status, tessdata_script_dir)) {
            LOG4CXX_ERROR(hw_logger_, "[" + job.job_name + "] Could not run tesseract!");
            boost::filesystem::remove_all(temp_im_directory);
            return job_status;
        }

        for (auto ocr_out: ocr_outputs) {
            MPFGenericTrack generic_track(ocr_out.confidence);
            // Copy over OSD results into OCR tracks.
            generic_track.detection_properties = osd_track_results.detection_properties;

            bool process_text = process_text_tagging(generic_track.detection_properties, c_job, ocr_out, job_status,
                                                     ocr_fset,
                                                     json_kvs_regex, json_kvs_string_split, json_kvs_string, page_num);
            if (process_text) {
                tracks.push_back(generic_track);
            }
        }
        page_num++;
    }

    boost::filesystem::remove_all(temp_im_directory);
    LOG4CXX_INFO(hw_logger_,
                 "[" + job.job_name + "] Processing complete. Found " + to_string(tracks.size()) + " tracks.");
    return job_status;
}

MPF_COMPONENT_CREATOR(TesseractOCRTextDetection);
MPF_COMPONENT_DELETER();
