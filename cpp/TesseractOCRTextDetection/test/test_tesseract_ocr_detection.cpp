/******************************************************************************
 * NOTICE                                                                     *
 *                                                                            *
 * This software (or technical data) was produced for the U.S. Government     *
 * under contract, and is subject to the Rights in Data-General Clause        *
 * 52.227-14, Alt. IV (DEC 2007).                                             *
 *                                                                            *
 * Copyright 2018 The MITRE Corporation. All Rights Reserved.                 *
 ******************************************************************************/

/******************************************************************************
 * Copyright 2018 The MITRE Corporation                                       *
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

#include <gtest/gtest.h>

#include "TesseractOCRTextDetection.h"

using namespace MPF::COMPONENT;


MPFImageJob createOCRJob(const std::string &uri){
	Properties algorithm_properties;
    Properties media_properties;
    std::string job_name("OCR_test");
    algorithm_properties["TAGGING_FILE"] = "text-tags.json";
    algorithm_properties["SHARPEN"] = "1.0";
    MPFImageJob job(job_name, uri, algorithm_properties, media_properties);
    return job;
}

/*
bool containsTextTag(const std::string &text, const std::vector<MPFImageLocation> &locations) {
    for (int i = 0; i < locations.size(); i++) {
        std::string text = locations[i].detection_properties.at("TEXT");
        
        if(text.find(text)!= std::string::npos)
            return true;
    }
    return false;
}

void assertObjectDetectedInImage(const std::string &expected_text, const std::string &image_path,
                                 TesseractOCRTextDetection &ocr) {
    MPFImageJob job = createOCRJob(image_path);

    std::vector<MPFImageLocation> image_locations;
    MPFDetectionError rc = ocr.GetDetections(job, image_locations);

    ASSERT_EQ(rc, MPF_DETECTION_SUCCESS);
    ASSERT_FALSE(image_locations.empty());

    ASSERT_TRUE(containsTextTag(expected_text, image_locations))
                                << "Expected OCR to detect text \"" << expected_text << "\" in " << image_path;
}
void assertObjectNotDetectedInImage(const std::string &expected_text, const std::string &expected_tags, const std::string &image_path,
                                 TesseractOCRTextDetection &ocr) {
    MPFImageJob job = createOCRJob(image_path);

    std::vector<MPFImageLocation> image_locations;
    MPFDetectionError rc = ocr.GetDetections(job, image_locations);

    ASSERT_EQ(rc, MPF_DETECTION_SUCCESS);
    ASSERT_FALSE(image_locations.empty());

    ASSERT_FALSE(containsTextTag(expected_text, image_locations))
                                << "Expected OCR to NOT detect text \"" << expected_text << "\" in " << image_path;
}*/


void runDetections(const std::string &image_path, TesseractOCRTextDetection &ocr, std::vector<MPFImageLocation> &image_locations) {
    MPFImageJob job = createOCRJob(image_path);
    MPFDetectionError rc = ocr.GetDetections(job, image_locations);

    ASSERT_EQ(rc, MPF_DETECTION_SUCCESS);
    ASSERT_FALSE(image_locations.empty());
}

void assertEmptyDetection(const std::string &image_path, TesseractOCRTextDetection &ocr, std::vector<MPFImageLocation> &image_locations) {
    MPFImageJob job = createOCRJob(image_path);
    MPFDetectionError rc = ocr.GetDetections(job, image_locations);

    ASSERT_EQ(rc, MPF_DETECTION_SUCCESS);
    ASSERT_TRUE(image_locations.empty());
}

bool containsText(const std::string &exp_text, const std::vector<MPFImageLocation> &locations) {
    for (int i = 0; i < locations.size(); i++) {
        std::string text = locations[i].detection_properties.at("TEXT");
        if(text.find(exp_text)!= std::string::npos)
            return true;
    }
    return false;
}

bool containsTag(const std::string &exp_tag, const std::vector<MPFImageLocation> &locations) {
    for (int i = 0; i < locations.size(); i++) {
        std::string text = locations[i].detection_properties.at("TAGS");
        
        if(text.find(exp_tag)!= std::string::npos)
            return true;
    }
    return false;
}

void assertTextInImage(const std::string &image_path, const std::string &expected_text, const std::vector<MPFImageLocation> &locations) {
    ASSERT_TRUE(containsText(expected_text, locations))
                               << "Expected OCR to detect text \"" << expected_text << "\" in " << image_path;
}

void assertTagInImage(const std::string &image_path, const std::string &expected_tag, const std::vector<MPFImageLocation> &locations) {
    ASSERT_TRUE(containsTag(expected_tag, locations))
                               << "Expected OCR to detect tag \"" << expected_tag << "\" in " << image_path;
}


void assertTextNotInImage(const std::string &image_path, const std::string &expected_text, const std::vector<MPFImageLocation> &locations) {
    ASSERT_FALSE(containsText(expected_text, locations))
                               << "Expected OCR to NOT detect text \"" << expected_text << "\" in " << image_path;
}

void assertTagNotInImage(const std::string &image_path, const std::string &expected_tag, const std::vector<MPFImageLocation> &locations) {
    ASSERT_FALSE(containsTag(expected_tag, locations))
                               << "Expected OCR to NOT detect tag \"" << expected_tag << "\" in " << image_path;
}

TEST(TESSERACTOCR, ImageTest) {

    TesseractOCRTextDetection ocr;
    ocr.SetRunDirectory("../plugin");
    std::vector<MPFImageLocation> results;

    ASSERT_TRUE(ocr.Init());

    runDetections("data/text-demo.png", ocr, results);
    assertTextInImage("data/text-demo.png", "TESTING 123", results);
    assertTextNotInImage("data/text-demo.png", "Ponies", results);
    assertTagNotInImage("data/text-demo.png", "personal", results);
    results.clear();

    runDetections("data/tags-keyword.png", ocr, results);
    assertTextInImage("data/text-demo.png", "Passenger Passport", results);
    assertTagInImage("data/text-demo.png", "identity document, travel", results);
    results.clear();


    runDetections("data/tags-keywordregex.png", ocr, results);
    assertTagInImage("data/text-demo.png", "personal", results);
    results.clear();

    runDetections("data/tags-regex.png", ocr, results);
    assertTagInImage("data/text-demo.png", "financial, personal", results);
    results.clear();

    assertEmptyDetection("data/blank.png", ocr, results);


    ASSERT_TRUE(ocr.Close());
}







