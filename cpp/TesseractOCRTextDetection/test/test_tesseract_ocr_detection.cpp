/******************************************************************************
 * NOTICE                                                                     *
 *                                                                            *
 * This software (or technical data) was produced for the U.S. Government     *
 * under contract, and is subject to the Rights in Data-General Clause        *
 * 52.227-14, Alt. IV (DEC 2007).                                             *
 *                                                                            *
 * Copyright 2020 The MITRE Corporation. All Rights Reserved.                 *
 ******************************************************************************/

/******************************************************************************
 * Copyright 2020 The MITRE Corporation                                       *
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

#include <string>
#include <MPFDetectionComponent.h>
#include <unistd.h>
#include <gtest/gtest.h>

#define BOOST_NO_CXX11_SCOPED_ENUMS

#include <boost/filesystem.hpp>

#undef BOOST_NO_CXX11_SCOPED_ENUMS

#include "TesseractOCRTextDetection.h"

using namespace MPF::COMPONENT;


void setAlgorithmProperties(Properties &algorithm_properties, const std::map<std::string, std::string> &custom) {
    algorithm_properties["TAGGING_FILE"] = "config/test-text-tags-foreign.json";
    algorithm_properties["STRUCTURED_TEXT_SHARPEN"] = "-1.0";
    algorithm_properties["TESSERACT_LANGUAGE"] = "eng";
    algorithm_properties["THRS_FILTER"] = "false";
    algorithm_properties["HIST_FILTER"] = "false";
    for (auto const &x : custom) {
        algorithm_properties[x.first] = x.second;
    }
}

MPFImageJob createImageJob(const std::string &uri, const std::map<std::string, std::string> &custom = {},
                           bool wild_mode = false) {
    Properties algorithm_properties;
    Properties media_properties;
    std::string job_name("OCR_test");
    setAlgorithmProperties(algorithm_properties, custom);
    if (wild_mode) {
        MPFImageLocation image_location(0, 0, 1, 1, -1);
        image_location.detection_properties["TEXT_TYPE"] = "UNSTRUCTURED";
        MPFImageJob job(job_name, uri, image_location, algorithm_properties, media_properties);
        return job;
    }
    MPFImageJob job(job_name, uri, algorithm_properties, media_properties);
    return job;
}

MPFGenericJob createPDFJob(const std::string &uri, const std::map<std::string, std::string> &custom = {}) {
    Properties algorithm_properties;
    Properties media_properties;
    std::string job_name("OCR_test");
    setAlgorithmProperties(algorithm_properties, custom);
    MPFGenericJob job(job_name, uri, algorithm_properties, media_properties);
    return job;
}

void runImageDetection(const std::string &image_path, TesseractOCRTextDetection &ocr,
                       std::vector<MPFImageLocation> &image_locations,
                       const std::map<std::string, std::string> &custom = {},
                       bool wild_mode = false) {
    MPFImageJob job = createImageJob(image_path, custom, wild_mode);
    image_locations = ocr.GetDetections(job);
    ASSERT_FALSE(image_locations.empty());
}

void runDocumentDetection(const std::string &image_path, TesseractOCRTextDetection &ocr,
                          std::vector<MPFGenericTrack> &generic_tracks,
                          const std::map<std::string, std::string> &custom = {}) {
    MPFGenericJob job = createPDFJob(image_path, custom);
    generic_tracks = ocr.GetDetections(job);
    ASSERT_FALSE(generic_tracks.empty());
}

void assertEmptyImageDetection(const std::string &image_path, TesseractOCRTextDetection &ocr,
                          std::vector<MPFImageLocation> &image_locations,
                          const std::map<std::string, std::string> &custom = {},
                          bool wild_mode = false, MPFDetectionError error = MPF_DETECTION_SUCCESS) {
    MPFImageJob job = createImageJob(image_path, custom, wild_mode);
    try {
        image_locations = ocr.GetDetections(job);
        if (error != MPF_DETECTION_SUCCESS) {
            FAIL() << "Expected error type: " << error;
        }
    }
    catch (const MPFDetectionException &ex) {
        ASSERT_EQ(error, ex.error_code);
    }
    catch (...) {
        FAIL() << "Caught exception but expected error type: " << error;
    }
    ASSERT_TRUE(image_locations.empty());
}

void assertEmptyDocumentDetection(const std::string &image_path, TesseractOCRTextDetection &ocr,
                          std::vector<MPFGenericTrack> &generic_tracks,
                          const std::map<std::string, std::string> &custom = {},
                          MPFDetectionError error = MPF_DETECTION_SUCCESS) {
    MPFGenericJob job = createPDFJob(image_path, custom);
    try {
        generic_tracks = ocr.GetDetections(job);
        if (error != MPF_DETECTION_SUCCESS) {
            FAIL() << "Expected error type: " << error;
        }
    }
    catch (const MPFDetectionException &ex) {
        ASSERT_EQ(error, ex.error_code);
    }
    catch (...) {
        FAIL() << "Caught exception but expected error type: " << error;
    }

    ASSERT_TRUE(generic_tracks.empty());
}

bool containsProp(const std::string &exp_text, const std::vector<MPFImageLocation> &locations, 
                  const std::string &property, int index = -1) {
    if (index != -1) {
        if (locations[index].detection_properties.count(property) == 0) {
            return false;
        }
        std::string text = locations[index].detection_properties.at(property);
        return text.find(exp_text) != std::string::npos;
    }
    for (int i = 0; i < locations.size(); i++) {
        if (locations[i].detection_properties.count(property) == 0) {
            continue;
        }
        std::string text = locations[i].detection_properties.at(property);
        if (text.find(exp_text) != std::string::npos)
            return true;
    }
    return false;
}


void assertInImage(const std::string &image_path, const std::string &expected_value,
                      const std::vector<MPFImageLocation> &locations, const std::string &prop, int index = -1) {
    ASSERT_TRUE(containsProp(expected_value, locations, prop, index))
                                << "Expected OCR to detect " << prop << " \"" << expected_value << "\" in " << image_path;
}

void assertNotInImage(const std::string &image_path, const std::string &expected_text,
                          const std::vector<MPFImageLocation> &locations, const std::string &prop, int index = -1) {
    ASSERT_FALSE(containsProp(expected_text, locations, prop, index))
                                << "Expected OCR to NOT detect "<< prop << " \""  << expected_text << "\" in "
                                << image_path;
}

void convert_results(std::vector<MPFImageLocation> &im_track, const std::vector<MPFGenericTrack> &gen_track) {
    for (int i = 0; i < gen_track.size(); i++) {
        MPFImageLocation image_location(0, 0, 1, 1);
        image_location.detection_properties = gen_track[i].detection_properties;
        im_track.push_back(image_location);
    }
}

TEST(TESSERACTOCR, ImageProcessingTest) {

    // Ensure contrast and unstructured image processing settings are enabled.

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());

    std::map<std::string,std::string> custom_properties = {};

    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/limited-contrast.png", ocr, results,  custom_properties));
    assertNotInImage("data/limited-contrast.png", "Contrast Text", results, "TEXT");

    results.clear();
    custom_properties = {{"MIN_HEIGHT", "-1"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/wild-small-text.png", ocr, results,  custom_properties));
    assertNotInImage("data/wild-small-text.png", "PLACE", results, "TEXT");

    results.clear();
    custom_properties = {{"MIN_HEIGHT", "60"}};
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blurry.png", ocr, results,  custom_properties));

    results.clear();
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/gradient.png", ocr, results,  custom_properties));
    assertNotInImage("data/gradient.png", "obscured", results, "TEXT");

    results.clear();
    custom_properties = {{"UNSTRUCTURED_TEXT_ENABLE_HIST_EQUALIZATION", "true"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/limited-contrast.png", ocr, results,  custom_properties, true));
    assertInImage("data/limited-contrast.png", "Contrast Text", results, "TEXT");

    results.clear();
    custom_properties = {{"UNSTRUCTURED_TEXT_ENABLE_ADAPTIVE_HIST_EQUALIZATION", "true"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/limited-contrast.png", ocr, results,  custom_properties, true));
    assertInImage("data/limited-contrast.png", "Contrast Text", results, "TEXT");

    results.clear();
    custom_properties = {{"MIN_HEIGHT", "60"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/wild-small-text.png", ocr, results,  custom_properties, true));
    assertInImage("data/wild-small-text.png", "PLACE", results, "TEXT");

    results.clear();
    custom_properties = {{"MIN_HEIGHT",             "-1"},
                         {"STRUCTURED_TEXT_SCALE",  "3.0"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/wild-small-text.png", ocr, results,  custom_properties));
    assertInImage("data/wild-small-text.png", "PLACE", results, "TEXT");

    results.clear();
    custom_properties = {{"STRUCTURED_TEXT_SHARPEN", "1.4"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/blurry.png", ocr, results,  custom_properties));
    assertInImage("data/blurry.png", "blurred text", results, "TEXT");

    results.clear();
    custom_properties = {{"STRUCTURED_TEXT_ENABLE_ADAPTIVE_THRS", "true"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/gradient.png", ocr, results,  custom_properties));
    assertInImage("data/gradient.png", "obscured", results, "TEXT");

    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, ModelTest) {

    // Ensure user can specify custom model directory locations.
    boost::filesystem::remove_all("data/model_dir");
    boost::filesystem::create_directories("data/model_dir/TesseractOCRTextDetection/tessdata");

    std::string model = boost::filesystem::absolute(
            "../plugin/TesseractOCRTextDetection/tessdata/eng.traineddata").string();
    symlink(model.c_str(), "data/model_dir/TesseractOCRTextDetection/tessdata/custom.traineddata");

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());
    std::map<std::string, std::string> custom_properties = {{"TESSERACT_LANGUAGE",    "custom,eng"},
                                                            {"MODELS_DIR_PATH",       "data/model_dir"},
                                                            {"ENABLE_OSD_AUTOMATION", "false"}};

    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng.png", ocr, results, custom_properties));

    boost::filesystem::remove_all("data/model_dir");
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "custom")
                                << "Shared models directory not loaded.";
    ASSERT_TRUE(results[1].detection_properties.at("TEXT_LANGUAGE") == "eng") << "Tessdata directory not loaded.";
    ASSERT_TRUE(results.size() == 2) << "Expected two models to be properly loaded into component.";

    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, MissingLanguagesTest) {

    // Double check missing languages during OSD processing.
    boost::filesystem::remove_all("data/model_dir");
    boost::filesystem::create_directories("data/model_dir/TesseractOCRTextDetection/tessdata/script");

    std::string model = boost::filesystem::absolute(
            "../plugin/TesseractOCRTextDetection/tessdata/eng.traineddata").string();
    symlink(model.c_str(), "data/model_dir/TesseractOCRTextDetection/tessdata/eng.traineddata");

    model = boost::filesystem::absolute(
                    "../plugin/TesseractOCRTextDetection/tessdata/osd.traineddata").string();
    symlink(model.c_str(), "data/model_dir/TesseractOCRTextDetection/tessdata/osd.traineddata");

    model = boost::filesystem::absolute(
                        "../plugin/TesseractOCRTextDetection/config").string();
    symlink(model.c_str(), "data/model_dir/TesseractOCRTextDetection/config");

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("data/model_dir");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());

    std::map<std::string, std::string> custom_properties = {{"TESSERACT_LANGUAGE",    "eng"},
                                                            {"MODELS_DIR_PATH",       "data/model_dir"}};

    // OSD image processing with all OSD language models missing.
    // Expected default language to be used.
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng-bul-small.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Cyrillic")
                                << "Expected Cyrillic as primary script.";
    ASSERT_TRUE(results[0].detection_properties.at("MISSING_LANGUAGE_MODELS") == "script/Cyrillic")
                                << "Expected Cyrillic script to be missing.";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "eng") << "Expected English script.";
    results.clear();

    // Adding Latin script model.
    model = boost::filesystem::absolute(
                    "../plugin/TesseractOCRTextDetection/tessdata/script/Latin.traineddata").string();
    symlink(model.c_str(), "data/model_dir/TesseractOCRTextDetection/tessdata/script/Latin.traineddata");

    // OSD image processing with some missing language models.
    // Latin will be used in place of Cyrillic as the next best available model.
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng-bul.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Cyrillic")
                                << "Expected Cyrillic as primary script.";
    ASSERT_TRUE(results[0].detection_properties.at("MISSING_LANGUAGE_MODELS") == "script/Cyrillic")
                                << "Expected Cyrillic script to be missing.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_SECONDARY_SCRIPTS") == "Latin") << "Expected Latin script.";

    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "script/Latin") << "Expected Latin script.";

    results.clear();

    custom_properties = {{"TESSERACT_LANGUAGE",    "eng"},
                         {"MODELS_DIR_PATH",       "data/model_dir"},
                         {"TESSERACT_PSM", "0"}};

    // OSD PDF processing with missing language models.
    // MISSING_LANGUAGE_MODELS should be populated on each page, with all OSD scripts not found by the component.
    std::vector<MPFGenericTrack> results_pdf;
    ASSERT_NO_FATAL_FAILURE(runDocumentDetection("data/osd-tests.pdf", ocr, results_pdf, custom_properties));
    convert_results(results, results_pdf);
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Han")
                                << "Expected Chinese/Han detected as primary script.";
    ASSERT_TRUE(results[0].detection_properties.at("MISSING_LANGUAGE_MODELS") == "script/Cyrillic")
                                << "Expected Cyrillic and Han scripts to be reported as missing.";

    ASSERT_TRUE(results[1].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Cyrillic")
                                << "Expected Cyrillic detected as primary script.";
    ASSERT_TRUE(results[0].detection_properties.at("MISSING_LANGUAGE_MODELS") == "script/Cyrillic")
                                << "Expected Cyrillic and Han scripts to be reported as missing.";

    results.clear();
    results_pdf.clear();

    // Double check proper error handling with missing languages (parallel processing).
    custom_properties = {{"TESSERACT_LANGUAGE",    "does_not_exist,testing"},
                         {"MODELS_DIR_PATH",       "data/model_dir"},
                         {"ENABLE_OSD_AUTOMATION", "false"}};

    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/eng-bul-small.png", ocr, results,
                                                      custom_properties, false, MPF_COULD_NOT_OPEN_DATAFILE));
    results.clear();
    results_pdf.clear();

    ASSERT_NO_FATAL_FAILURE(assertEmptyDocumentDetection("data/osd-tests.pdf", ocr, results_pdf,
                                                         custom_properties, MPF_COULD_NOT_OPEN_DATAFILE));
    results.clear();
    results_pdf.clear();

    // Double check proper error handling with missing languages (serial processing).
    custom_properties = {{"TESSERACT_LANGUAGE",    "does_not_exist,testing"},
                         {"MODELS_DIR_PATH",       "data/model_dir"},
                         {"ENABLE_OSD_AUTOMATION", "false"},
                         {"MAX_PARALLEL_PAGE_THREADS", "0"},
                         {"MAX_PARALLEL_SCRIPT_THREADS", "0"}};

    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/eng-bul-small.png", ocr, results,
                                                      custom_properties, false, MPF_COULD_NOT_OPEN_DATAFILE));
    results.clear();
    results_pdf.clear();

    ASSERT_NO_FATAL_FAILURE(assertEmptyDocumentDetection("data/osd-tests.pdf", ocr, results_pdf,
                                                         custom_properties, MPF_COULD_NOT_OPEN_DATAFILE));
    results.clear();
    results_pdf.clear();

    boost::filesystem::remove_all("data/model_dir");
    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, RescaleTest) {

    // Ensure proper rescaling of images within Tesseract limits.
    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());

    std::map<std::string,std::string> custom_properties = {};

    // Expected image to be discarded due to invalid dimensions.
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank_exceed_tesseract_limit.png", ocr, results,
                            custom_properties, false, MPF_BAD_FRAME_SIZE));

    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank_tesseract_limit.png", ocr, results,
                                custom_properties, false, MPF_BAD_FRAME_SIZE));

    // If narrow height is not checked allow image to process.
    custom_properties = {{"INVALID_MIN_IMAGE_SIZE", "-1"}};
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank_tesseract_limit.png", ocr, results,
                            custom_properties));
    results.clear();

    // Image should be rescaled to fit.
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank_tesseract_rescalable.png", ocr, results,
                            custom_properties));
    results.clear();

    // Ensure image scaling is capped at a certain point.
    custom_properties = {{"STRUCTURED_TEXT_SCALE", "100000000"}};
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank_within_tesseract_limit.png", ocr, results,
                            custom_properties));
    results.clear();

    // Ensure image scaling is capped at a certain point.
    custom_properties = {{"STRUCTURED_TEXT_SCALE", "0.00000001"}};
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank_within_tesseract_limit.png", ocr, results,
                            custom_properties));
    results.clear();

    // Image should be rescaled to fit pixel constraints.
    custom_properties = {{"MAX_PIXELS", "120000"}, {"STRUCTURED_TEXT_SCALE", "100000000"}};
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank.png", ocr, results,
                            custom_properties));
    results.clear();

    // Image should be rescaled to fit pixel constraints.
    custom_properties = {{"MAX_PIXELS", "120000"}, {"STRUCTURED_TEXT_SCALE", "0.00000001"}};
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank.png", ocr, results,
                            custom_properties));
    results.clear();

    // Sanity check that rescaling is called outside of OSD processing when OSD is disabled.
    // Image should be rescaled to fit.
    custom_properties = {{"ENABLE_OSD_AUTOMATION", "false"}};
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank_tesseract_rescalable.png", ocr, results,
                            custom_properties));
    results.clear();

    // Ensure image scaling is capped at a certain point.
    custom_properties = {{"STRUCTURED_TEXT_SCALE", "100000000"}, {"ENABLE_OSD_AUTOMATION", "false"}};
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank_within_tesseract_limit.png", ocr, results,
                            custom_properties));
    results.clear();

    // Ensure image scaling is capped at a certain point.
    custom_properties = {{"STRUCTURED_TEXT_SCALE", "0.00000001"}, {"ENABLE_OSD_AUTOMATION", "false"}};
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank_within_tesseract_limit.png", ocr, results,
                            custom_properties));
    results.clear();

    // Expected rescale failure due to min image size constraint.
    custom_properties = {{"INVALID_MIN_IMAGE_SIZE", "60"}};
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank_tesseract_rescalable.png", ocr, results,
                                custom_properties, false, MPF_BAD_FRAME_SIZE));
    results.clear();

    // Expected rescale failure due to max pixels and min image size conflict.
    custom_properties = {{"MAX_PIXELS", "10"}};
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank_within_tesseract_limit.png", ocr, results,
                            custom_properties, false, MPF_BAD_FRAME_SIZE));
    results.clear();
}

TEST(TESSERACTOCR, TwoPassOCRTest) {

    // Ensure that two pass OCR correctly works to process upside down text even w/out OSD support.

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());

    std::map<std::string, std::string> custom_properties = {{"ENABLE_OSD_AUTOMATION", "false"},
                                                            {"ROTATE_AND_DETECT",     "true"}};

    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng.png", ocr, results, custom_properties));
    ASSERT_TRUE(std::stoi(results[0].detection_properties.at("ROTATE_AND_DETECT_PASS")) == 0) << "Expected 0 degree text rotation correction.";
    assertInImage("data/eng.png", "All human beings", results, "TEXT");
    results.clear();

    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng-rotated.png", ocr, results, custom_properties));
    ASSERT_TRUE(std::stoi(results[0].detection_properties.at("ROTATE_AND_DETECT_PASS")) == 180) << "Expected 180 degree text rotation correction.";
    assertInImage("data/eng-rotated.png", "All human beings", results, "TEXT");
    results.clear();

    // Test to make sure threshold parameter is working.
    custom_properties = {{"ENABLE_OSD_AUTOMATION",            "false"},
                         {"ROTATE_AND_DETECT_MIN_OCR_CONFIDENCE", "10"},
                         {"ROTATE_AND_DETECT",                "true"}};

    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng-rotated.png", ocr, results, custom_properties));
    ASSERT_TRUE(std::stoi(results[0].detection_properties.at("ROTATE_AND_DETECT_PASS")) == 0) << "Expected 0 degree text rotation correction.";
    assertNotInImage("data/eng-rotated.png", "All human beings", results, "TEXT");
    ASSERT_TRUE(ocr.Close());

}

TEST(TESSERACTOCR, TrackFilterTest) {

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());
    std::map<std::string, std::string> custom_properties = {{"ENABLE_OSD_AUTOMATION", "false"},
                                                            {"TESSERACT_LANGUAGE",    "eng,deu"},
                                                            {"MAX_TEXT_TRACKS",       "1"}};

    // Check that the top text track is properly returned after filtering.
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "eng") << "Top text track not returned.";
    ASSERT_TRUE(results.size() == 1) << "Expected output track only.";
    results.clear();

    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, TaggingTest) {

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());
    std::map<std::string, std::string> custom_properties = {{"ENABLE_OSD_AUTOMATION", "false"}};
    std::map<std::string, std::string> custom_properties_disabled = {{"ENABLE_OSD_AUTOMATION", "false"},
                                                                     {"FULL_REGEX_SEARCH", "false"}};

    // Test basic text detection.
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/text-demo.png", ocr, results, custom_properties));
    assertInImage("data/text-demo.png", "TESTING 123", results, "TEXT");
    assertNotInImage("data/text-demo.png", "Ponies", results, "TEXT");
    assertNotInImage("data/text-demo.png", "personal", results, "TAGS");
    ASSERT_TRUE(results[0].detection_properties.at("TAGS").length() == 0);
    results.clear();

    // Test multiple text tagging.
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/tags-keyword.png", ocr, results, custom_properties));
    assertInImage("data/tags-keyword.png", "Passenger Passport", results, "TEXT");
    assertInImage("data/tags-keyword.png", "identity document; travel", results, "TAGS");
    assertInImage("data/tags-keyword.png", "Passenger; Passport", results, "TRIGGER_WORDS");
    assertInImage("data/tags-keyword.png", "0-8; 10-17", results, "TRIGGER_WORDS_OFFSET");
    results.clear();

    // Test multiple text tagging.
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/tags-regex.png", ocr, results, custom_properties));
    assertInImage("data/tags-regex.png", "case-insensitive-tag; financial; personal", results, "TAGS");
    assertInImage("data/tags-regex.png", "122-123-1234; financ", results, "TRIGGER_WORDS");
    assertInImage("data/tags-regex.png", "17-28; 0-5", results, "TRIGGER_WORDS_OFFSET");
    results.clear();

    // Test regex tagging with full search enabled.
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/tags-keywordregex.png", ocr, results, custom_properties));
    assertInImage("data/tags-keywordregex.png", "case-insensitive-tag; case-sensitive-tag; financial; personal; vehicle",
                  results, "TAGS");
    assertInImage("data/tags-keywordregex.png", "01/01/20; Financ; Text; Vehicle", results, "TRIGGER_WORDS");
    assertInImage("data/tags-keywordregex.png", "20-27; 37-42; 10-13, 15-18; 29-35", results, "TRIGGER_WORDS_OFFSET");
    results.clear();

    // With full regex search disabled, number of reported triggers and offsets will decrease.
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/tags-keywordregex.png", ocr, results, custom_properties_disabled));
    assertInImage("data/tags-keywordregex.png", "case-insensitive-tag; case-sensitive-tag; financial; personal; vehicle",
                  results, "TAGS");
    assertInImage("data/tags-keywordregex.png", "01/01/20; Financ; Vehicle", results, "TRIGGER_WORDS");
    assertInImage("data/tags-keywordregex.png", "20-27; 37-42; 29-35", results, "TRIGGER_WORDS_OFFSET");
    results.clear();

    // Test multiple text tagging w/ delimiter tag.
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/tags-regex-delimiter.png", ocr, results, custom_properties));
    assertInImage("data/tags-regex-delimiter.png", "case-insensitive-tag; delimiter-test; financial; personal",
                  results, "TAGS");
    assertInImage("data/tags-regex-delimiter.png", "122-123-1234; a[[;] ]b; financ", results, "TRIGGER_WORDS");
    assertInImage("data/tags-regex-delimiter.png", "22-33; 15-20; 0-5", results, "TRIGGER_WORDS_OFFSET");

    // Test escaped backslash text tagging.
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/test-backslash.png", ocr, results, custom_properties));
    assertInImage("data/test-backslash.png", "backslash; personal", results, "TAGS");
    assertInImage("data/test-backslash.png", "TEXT; \\", results, "TRIGGER_WORDS");
    assertInImage("data/test-backslash.png", "7-10; 0, 12, 15, 16, 18, 19", results, "TRIGGER_WORDS_OFFSET");

    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, BlankTest) {

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());
    std::map<std::string, std::string> custom_properties = {{"ENABLE_OSD_AUTOMATION", "false"}};

    // Check no text detected for blank image.
    ASSERT_NO_FATAL_FAILURE(assertEmptyImageDetection("data/blank.png", ocr, results, custom_properties));

    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, OSDTest) {

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());

    // Check orientation detection. Text should be properly extracted.
    std::map<std::string, std::string> custom_properties = {{"ENABLE_OSD_AUTOMATION",             "true"},
                                                            {"MIN_OSD_PRIMARY_SCRIPT_CONFIDENCE", "0.30"},
                                                            {"MIN_OSD_SCRIPT_SCORE",              "40"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng-rotated.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "180") << "Expected 180 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Latin") << "Expected Latin script.";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "script/Latin") << "Expected latin script.";
    assertInImage("data/eng-rotated.png", "All human beings", results, "TEXT");

    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, OSDConfidenceFilteringTest) {
    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());

    // Check script confidence filtering.
    // By setting the min script confidence level too high, the component must default back to original setting (eng).
    // Alternatively, setting the min script score too high would also work.
    std::map<std::string, std::string> custom_properties = {{"ENABLE_OSD_AUTOMATION",             "true"},
                                                            {"MIN_OSD_PRIMARY_SCRIPT_CONFIDENCE", "100"},
                                                            {"MIN_OSD_SCRIPT_SCORE",              "0"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Latin") << "Expected Latin script.";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "eng")
                                << "Expected default language (eng) due to script confidence rejection.";
    assertInImage("data/eng.png", "All human beings", results, "TEXT");
    results.clear();

    // Check script score filtering with OSD fallback enabled.
    // Check that OSD fallback now raises detected script (script/Latin) confidence scores to acceptable threshold.
    custom_properties = {{"ENABLE_OSD_AUTOMATION",             "true"},
                         {"ENABLE_OSD_FALLBACK",               "true"},
                         {"MIN_OSD_PRIMARY_SCRIPT_CONFIDENCE", "0"},
                         {"MIN_OSD_SCRIPT_SCORE",              "60"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Latin") << "Expected Latin script.";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "script/Latin")
                                << "Expected Latin script due to OSD fallback improving confidence scores.";
    assertInImage("data/eng.png", "All human beings", results, "TEXT");
    results.clear();

    // Check orientation confidence filtering.
    // In the OSDTest for eng-rotated.png, the component will accept a detected rotation of 180 degrees.
    // By setting the min rotation confidence level too high, the component must default back to original setting (0 degrees rotation).
    custom_properties = {{"TESSERACT_PSM",               "0"},
                         {"MIN_OSD_TEXT_ORIENTATION_CONFIDENCE", "200"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng-rotated.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected default text rotation.";

    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, OSDCommonTest) {

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());

    // Check that "Common" script is reported but secondary script is used to run OCR.
    std::map<std::string, std::string> custom_properties = {{"ENABLE_OSD_AUTOMATION",              "true"},
                                                            {"MIN_OSD_PRIMARY_SCRIPT_CONFIDENCE",  "0.0"},
                                                            {"MIN_OSD_SCRIPT_SCORE",               "0"},
                                                            {"MAX_OSD_SCRIPTS",                    "1"},
                                                            {"MIN_OSD_SECONDARY_SCRIPT_THRESHOLD", "0.0"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/numeric.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Common") << "Expected Common script.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_SECONDARY_SCRIPTS") == "Cyrillic") << "Expected Cyrillic script.";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "script/Cyrillic")
                                << "Expected Cyrillic script.";

    results.clear();

    // Check that "Common" script is reported but default language is used to run OCR.
    custom_properties = {{"ENABLE_OSD_AUTOMATION", "true"},
                         {"MAX_OSD_SCRIPTS",       "1"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/numeric.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Common") << "Expected Common script.";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "eng") << "Expected default language (eng).";

    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, OSDFallbackTest) {
    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());
    std::map<std::string, std::string> custom_properties;

    // Check that OSD fallback works to detect proper scripts.
    custom_properties = {{"ENABLE_OSD_AUTOMATION",              "true"},
                         {"MIN_OSD_PRIMARY_SCRIPT_CONFIDENCE",  "0.0"},
                         {"MIN_OSD_SCRIPT_SCORE",               "0"},
                         {"MAX_OSD_SCRIPTS",                    "2"},
                         {"MIN_OSD_SECONDARY_SCRIPT_THRESHOLD", "0.0"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/text-insufficient.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Cyrillic") << "Expected Cyrillic script.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_SECONDARY_SCRIPTS") == "Latin") << "Expected Latin script.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_FALLBACK_OCCURRED") == "true")
                                                   << "Expected OSD fallback had occurred.";
    results.clear();

    // When OSD fallback is disabled, no script is detected.
    custom_properties = {{"ENABLE_OSD_AUTOMATION",              "true"},
                         {"MIN_OSD_PRIMARY_SCRIPT_CONFIDENCE",  "0.0"},
                         {"MIN_OSD_SCRIPT_SCORE",               "0"},
                         {"MAX_OSD_SCRIPTS",                    "2"},
                         {"ENABLE_OSD_FALLBACK",                "false"},
                         {"MIN_OSD_SECONDARY_SCRIPT_THRESHOLD", "0.0"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/text-insufficient.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "NULL") << "Expected no script detected.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_FALLBACK_OCCURRED") == "false")
                                                   << "Expected no OSD fallback had occurred.";
    results.clear();

    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, OSDMultilanguageScriptTest) {

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());

    // Check multi-language script detection under serial processing conditions.
    // Individual scripts are run separately.
    // Set max scripts to three, however based on filters only two scripts should be processed as a single track.
    std::map<std::string, std::string> custom_properties = {{"ENABLE_OSD_AUTOMATION", "true"},
                                                            {"MAX_OSD_SCRIPTS",       "3"},
                                                            {"MIN_OSD_SCRIPT_SCORE",  "45.0"},
                                                            {"MAX_PARALLEL_SCRIPT_THREADS", "0"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng-bul-small.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Cyrillic")
                                << "Expected Cyrillic as primary script.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_SECONDARY_SCRIPTS") == "Latin")
                                << "Expected Latin as secondary script.";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "script/Cyrillic+script/Latin")
                                << "Expected both scripts to run together.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_FALLBACK_OCCURRED") == "false")
                                << "Expected no OSD fallback had occurred.";
    assertInImage("data/eng-bul-small.png", "Всички хора се раждат ", results, "TEXT");
    assertInImage("data/eng-bul-small.png", "All human beings", results, "TEXT");
    results.clear();

    // Check multi-language script detection under parallel processing conditions.
    // Individual scripts are run separately.
    custom_properties = {{"ENABLE_OSD_AUTOMATION", "true"},
                         {"MAX_OSD_SCRIPTS",       "3"},
                         {"COMBINE_OSD_SCRIPTS",   "false"},
                         {"MIN_OSD_SCRIPT_SCORE",  "45.0"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng-bul-small.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Cyrillic")
                                << "Expected Cyrillic as primary script.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_SECONDARY_SCRIPTS") == "Latin")
                                << "Expected Latin as secondary script.";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "script/Cyrillic")
                                << "Expected both scripts to run separately.";

    ASSERT_TRUE(results[1].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[1].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Cyrillic")
                                << "Expected Cyrillic as primary script.";
    ASSERT_TRUE(results[1].detection_properties.at("OSD_SECONDARY_SCRIPTS") == "Latin")
                                << "Expected Latin as secondary script.";
    ASSERT_TRUE(results[1].detection_properties.at("TEXT_LANGUAGE") == "script/Latin")
                                << "Expected both scripts to run separately.";

    // Test multilanguage text extraction.
    assertInImage("data/eng-bul-small.png", "foreign-text", results, "TAGS", 0);
    assertInImage("data/eng-bul-small.png", "свободни", results, "TRIGGER_WORDS", 0);
    assertInImage("data/eng-bul-small.png", "107-114", results, "TRIGGER_WORDS_OFFSET", 0);
    assertInImage("data/eng-bul-small.png", "Всички хора се раждат свободни", results, "TEXT", 0);

    // Also test mult-keyword phrase tag.
    assertInImage("data/eng-bul-small.png", "key-phrase", results, "TAGS", 1);
    assertInImage("data/eng-bul-small.png", "brotherhood", results, "TRIGGER_WORDS", 1);
    assertInImage("data/eng-bul-small.png", "433-453", results, "TRIGGER_WORDS_OFFSET", 1);
    assertInImage("data/eng-bul-small.png", "All human beings are born free", results, "TEXT", 1);

    results.clear();

    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, OSDMultiPageTest) {

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());

    // Check multiple page OSD processing (script and orientation detection).
    std::vector<MPFGenericTrack> results_pdf;
    std::map<std::string, std::string> custom_properties = {{"TESSERACT_PSM", "0"}};
    ASSERT_NO_FATAL_FAILURE(runDocumentDetection("data/osd-tests.pdf", ocr, results_pdf, custom_properties));
    convert_results(results, results_pdf);
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Han")
                                << "Expected Chinese/Han detected as primary script.";
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[1].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Cyrillic")
                                << "Expected Cyrillic detected as primary script.";
    ASSERT_TRUE(results[1].detection_properties.at("ROTATION") == "180") << "Expected 180 degree text rotation.";
    results_pdf.clear();
    results.clear();

    custom_properties = {{"ENABLE_OSD_AUTOMATION",             "true"},
                         {"MIN_OSD_PRIMARY_SCRIPT_CONFIDENCE", "0.30"},
                         {"ENABLE_OSD_FALLBACK",               "false"},
                         {"MIN_OSD_SCRIPT_SCORE",              "40"},
                         {"MAX_PARALLEL_PAGE_THREADS",         "3"}};

    // Check multiple page OSD processing.
    // Ensure that the default language is properly reset.

    ASSERT_NO_FATAL_FAILURE(runDocumentDetection("data/osd-check-defaults.pdf", ocr, results_pdf, custom_properties));
    convert_results(results, results_pdf);
    ASSERT_TRUE(results.size() == 2) << "Expected two text tracks (middle page is blank and should be ignored).";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Cyrillic") << "Expected Cyrillic script.";
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "script/Cyrillic")
                                << "Expected Cyrillic script.";
    ASSERT_TRUE(results[1].detection_properties.at("OSD_PRIMARY_SCRIPT") == "NULL")
                                << "Expected NULL script due to insufficient text.";
    ASSERT_TRUE(results[1].detection_properties.at("TEXT_LANGUAGE") == "eng")
                                << "Expected default language (eng) for second track.";

    results_pdf.clear();
    results.clear();

    custom_properties = {{"ENABLE_OSD_AUTOMATION",             "true"},
                        {"MIN_OSD_PRIMARY_SCRIPT_CONFIDENCE", "0.30"},
                        {"ENABLE_OSD_FALLBACK",               "false"},
                        {"MIN_OSD_SCRIPT_SCORE",              "40"},
                        {"MAX_PARALLEL_PAGE_THREADS", "0"}};

    // Verify proper processing behavior under serial conditions.
    ASSERT_NO_FATAL_FAILURE(runDocumentDetection("data/osd-check-defaults.pdf", ocr, results_pdf, custom_properties));
    convert_results(results, results_pdf);
    ASSERT_TRUE(results.size() == 2) << "Expected two text tracks (middle page is blank and should be ignored).";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Cyrillic") << "Expected Cyrillic script.";
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "script/Cyrillic")
                                << "Expected Cyrillic script.";
    ASSERT_TRUE(results[1].detection_properties.at("OSD_PRIMARY_SCRIPT") == "NULL")
                                << "Expected NULL script due to insufficient text.";
    ASSERT_TRUE(results[1].detection_properties.at("TEXT_LANGUAGE") == "eng")
                                << "Expected default language (eng) for second track.";

    results_pdf.clear();
    results.clear();

    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, OSDSecondaryScriptThresholdTest) {

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());
    // Check multi-language script detection.
    // Set max scripts to two, two scripts should be picked up and processed.
    std::map<std::string, std::string> custom_properties = {{"ENABLE_OSD_AUTOMATION", "true"},
                                                            {"MAX_OSD_SCRIPTS",       "2"},
                                                            {"MIN_OSD_SCRIPT_SCORE",  "30.0"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng-bul-small.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Cyrillic")
                                << "Expected Cyrillic as primary script.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_SECONDARY_SCRIPTS") == "Latin")
                                << "Expected Latin as secondary script.";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "script/Cyrillic+script/Latin")
                                << "Expected both Cyrillic and Latin scripts";

    results.clear();

    // Set max scripts to two, however by setting min secondary script threshold too high, only one script should be processed.
    custom_properties = {{"ENABLE_OSD_AUTOMATION",              "true"},
                         {"MAX_OSD_SCRIPTS",                    "2"},
                         {"MIN_OSD_SCRIPT_SCORE",               "45.0"},
                         {"MIN_OSD_SECONDARY_SCRIPT_THRESHOLD", "0.99"}};
    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng-bul-small.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Cyrillic")
                                << "Expected Cyrillic as primary script.";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "script/Cyrillic")
                                << "Expected only Cyrillic script due to threshold filtering.";

    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, OSDHanOrientationTest) {

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());
    std::map<std::string, std::string> custom_properties = {{"ENABLE_OSD_AUTOMATION", "true"},
                                                            {"MIN_OSD_SCRIPT_SCORE",  "45.0"},
                                                            {"MAX_TEXT_TRACKS",       "1"},
                                                            {"COMBINE_OSD_SCRIPTS",   "false"}};

    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/chinese.png", ocr, results, custom_properties));
    ASSERT_TRUE(results[0].detection_properties.at("ROTATION") == "0") << "Expected 0 degree text rotation.";
    ASSERT_TRUE(results[0].detection_properties.at("OSD_PRIMARY_SCRIPT") == "Han") << "Expected Chinese as primary script.";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "script/HanS+script/HanT")
                                << "Expected Chinese horizontal text to be the correct output model";

    ASSERT_TRUE(ocr.Close());
}


TEST(TESSERACTOCR, DocumentTest) {

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    std::vector<MPFGenericTrack> results_pdf;
    ASSERT_TRUE(ocr.Init());
    std::map<std::string, std::string> custom_properties = {{"ENABLE_OSD_AUTOMATION",     "false"},
                                                            {"MAX_PARALLEL_PAGE_THREADS", "3"}};

    // Test document text extraction.
    ASSERT_NO_FATAL_FAILURE(runDocumentDetection("data/test.pdf", ocr, results_pdf, custom_properties));
    convert_results(results, results_pdf);
    assertInImage("data/test.pdf", "personal", results, "TAGS", 0);
    assertInImage("data/test.pdf", "text", results, "TRIGGER_WORDS", 0);
    assertInImage("data/test.pdf", "This is a test", results, "TEXT", 0);
    assertInImage("data/test.pdf", "vehicle", results, "TAGS", 1);
    assertInImage("data/test.pdf", "Vehicle", results, "TRIGGER_WORDS", 1);
    assertInImage("data/test.pdf", "Vehicle", results, "TEXT", 1);
    assertInImage("data/test.pdf", "vehicle", results, "TAGS", 2);
    assertInImage("data/test.pdf", "Auto", results, "TRIGGER_WORDS", 2);
    assertInImage("data/test.pdf", "Automobile", results, "TEXT", 2);

    ASSERT_TRUE(ocr.Close());
}

TEST(TESSERACTOCR, RedundantFilterTest) {

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;
    ASSERT_TRUE(ocr.Init());

    // Check that redundant scripts are ignored properly and that one track is returned for each non-redundant entry.

    std::map<std::string, std::string> custom_properties = {{"ENABLE_OSD_AUTOMATION",             "false"},
                                                            {"TESSERACT_LANGUAGE",    "eng+eng,eng+eng,fra,fra+eng,eng+fra+fra"},
                                                            {"MAX_PARALLEL_SCRIPT_THREADS", "4"}};

    ASSERT_NO_FATAL_FAILURE(runImageDetection("data/eng.png", ocr, results, custom_properties));
    ASSERT_TRUE(results.size() == 4) << "Expected four text tracks (second input should be ignored).";
    ASSERT_TRUE(results[0].detection_properties.at("TEXT_LANGUAGE") == "eng") << "Expected redundant English script ignored.";
    ASSERT_TRUE(results[1].detection_properties.at("TEXT_LANGUAGE") == "eng+fra") << "Expected English followed by French script.";
    ASSERT_TRUE(results[2].detection_properties.at("TEXT_LANGUAGE") == "fra") << "Expected French script.";
    ASSERT_TRUE(results[3].detection_properties.at("TEXT_LANGUAGE") == "fra+eng") << "Expected French followed by English script.";

    ASSERT_TRUE(ocr.Close());
}
