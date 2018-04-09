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

#include <fstream>
#include <unordered_map>

#include <log4cxx/xml/domconfigurator.h>

#include <MPFImageReader.h>
#include <MPFVideoCapture.h>
#include <detectionComponentUtils.h>
#include <Utils.h>

#include "Trackers.h"
#include "DarknetDetection.h"

using namespace MPF::COMPONENT;


bool DarknetDetection::Init() {
    std::string run_dir = GetRunDirectory();
    if (run_dir.empty()) {
        run_dir = ".";
    }

    plugin_path_ = run_dir + "/DarknetDetection";

    log4cxx::xml::DOMConfigurator::configure(plugin_path_ + "/config/Log4cxxConfig.xml");
    logger_ = log4cxx::Logger::getLogger("DarknetDetection");

    try {
        models_parser_.Init(plugin_path_ + "/models")
                .RegisterField("network_config", &ModelSettings::network_config_file)
                .RegisterField("names", &ModelSettings::names_file)
                .RegisterField("weights", &ModelSettings::weights_file);
    }
    catch (const std::exception &ex) {
        LOG4CXX_ERROR(logger_, "Failed to initialize ModelsIniParser due to: " << ex.what())
        return false;
    }

    LOG4CXX_INFO(logger_, "Initialized DarknetDetection component.");

    return true;
}


MPFDetectionError DarknetDetection::GetDetections(const MPFVideoJob &job, std::vector<MPFVideoTrack> &tracks) {
    LOG4CXX_INFO(logger_, "[" << job.job_name << "] Starting job");
    if (DetectionComponentUtils::GetProperty(job.job_properties, "USE_PREPROCESSOR", false)) {
        PreprocessorTracker tracker;
        return GetDetections(job, tracks, tracker);
    }
    else {
        int number_of_classifications = DetectionComponentUtils::GetProperty(
                job.job_properties, "NUMBER_OF_CLASSIFICATIONS_PER_REGION", 5);
        SingleDetectionPerTrackTracker tracker(number_of_classifications);
        return GetDetections(job, tracks, tracker);
    }
}

template<typename Tracker>
MPFDetectionError DarknetDetection::GetDetections(const MPFVideoJob &job, std::vector<MPFVideoTrack> &tracks,
                                                  Tracker &tracker) {
    try {
        DarknetDl detector = GetDarknetImpl(job);

        MPFVideoCapture video_cap(job);
        if (!video_cap.IsOpened()) {
            LOG4CXX_ERROR(logger_, "[" << job.job_name << "] Could not open video file: " << job.data_uri);
            return MPF_COULD_NOT_OPEN_DATAFILE;
        }

        cv::Mat frame;
        int frame_number = -1;

        while (video_cap.Read(frame)) {
            frame_number++;
            tracker.ProcessFrameDetections(detector.Detect(frame), frame_number);
        }

        tracks = tracker.GetTracks();

        for (MPFVideoTrack &track : tracks) {
            video_cap.ReverseTransform(track);
        }

        LOG4CXX_INFO(logger_, "[" << job.job_name << "] Found " << tracks.size() << " tracks.");

        return MPF_DETECTION_SUCCESS;
    }
    catch (...) {
        return Utils::HandleDetectionException(job, logger_);
    }
}



MPFDetectionError DarknetDetection::GetDetections(const MPFImageJob &job, std::vector<MPFImageLocation> &locations) {
    try {
        LOG4CXX_INFO(logger_, "[" << job.job_name << "] Starting job");
        DarknetDl detector = GetDarknetImpl(job);

        MPFImageReader image_reader(job);

        std::vector<DarknetResult> results = detector.Detect(image_reader.GetImage());
        if (DetectionComponentUtils::GetProperty(job.job_properties, "USE_PREPROCESSOR", false)) {
            ConvertResultsUsingPreprocessor(results, locations);
        }
        else {
            int number_of_classifications
                    = std::max(1, DetectionComponentUtils::GetProperty(job.job_properties,
                                                                       "NUMBER_OF_CLASSIFICATIONS_PER_REGION", 5));
            for (auto& result : results) {
                locations.push_back(
                        SingleDetectionPerTrackTracker::CreateImageLocation(number_of_classifications, result));
            }
        }

        for (auto &location : locations) {
            image_reader.ReverseTransform(location);
        }

        LOG4CXX_INFO(logger_, "[" << job.job_name << "] Found " << locations.size() << " detections.");

        return MPF_DETECTION_SUCCESS;
    }
    catch (...) {
        return Utils::HandleDetectionException(job, logger_);
    }
}


DarknetDetection::DarknetDl DarknetDetection::GetDarknetImpl(const MPF::COMPONENT::MPFJob &job) {
    static std::string cpu_darknet_lib = plugin_path_ + "/lib/libdarknet_wrapper.so";
    static std::string gpu_darknet_lib = plugin_path_ + "/lib/libdarknet_wrapper_cuda.so";
    const ModelSettings model_settings = GetModelSettings(job.job_properties);

    int cuda_device_id = DetectionComponentUtils::GetProperty(job.job_properties, "CUDA_DEVICE_ID", -1);
    if (cuda_device_id < 0) {
        return DarknetDl(cpu_darknet_lib, job, model_settings);
    }

    try {
        return DarknetDl(gpu_darknet_lib, job, model_settings);
    }
    catch (const std::exception &ex) {
        if (DetectionComponentUtils::GetProperty(job.job_properties, "FALLBACK_TO_CPU_WHEN_GPU_PROBLEM", false)) {
            LOG4CXX_WARN(logger_, "An error occured while trying to load the GPU version of Darknet: " << ex.what());
            LOG4CXX_WARN(logger_, "Falling back to CPU version.");
            return DarknetDl(cpu_darknet_lib, job, model_settings);
        }
        else {
            throw;
        }

    }
}

ModelSettings DarknetDetection::GetModelSettings(const MPF::COMPONENT::Properties &job_properties) const {
    const std::string &model_name = DetectionComponentUtils::GetProperty<std::string>(
            job_properties, "MODEL_NAME", "tiny yolo");

    const std::string &models_dir_path = DetectionComponentUtils::GetProperty<std::string>(
            job_properties, "MODELS_DIR_PATH", ".");

    return models_parser_.ParseIni(model_name, models_dir_path + "/DarknetDetection");
}


void DarknetDetection::ConvertResultsUsingPreprocessor(std::vector<DarknetResult> &darknet_results,
                                                       std::vector<MPFImageLocation> &locations) {

    std::unordered_map<std::string, MPFImageLocation> type_to_image_loc;

    for (DarknetResult &darknet_result : darknet_results) {
        const cv::Rect &rect = darknet_result.detection_rect;

        for (std::pair<float, std::string> &class_prob : darknet_result.object_type_probs) {
            auto it = type_to_image_loc.find(class_prob.second);
            if (it == type_to_image_loc.cend()) {
                type_to_image_loc.emplace(
                        class_prob.second,
                        MPFImageLocation(rect.x, rect.y, rect.width, rect.height, class_prob.first,
                                         Properties{ {"CLASSIFICATION", class_prob.second} }));
            }
            else {
                PreprocessorTracker::CombineImageLocation(rect, class_prob.first, it->second);
            }
        }
    }

    for (auto &image_loc_pair : type_to_image_loc) {
        locations.push_back(std::move(image_loc_pair.second));
    }
}


std::string DarknetDetection::GetDetectionType() {
    return "CLASS";
}



bool DarknetDetection::Close() {
    return true;
}



DarknetDetection::DarknetDl::DarknetDl(const std::string &lib_path, const MPFJob &job,
                                       const ModelSettings &model_settings)
    : lib_handle_(dlopen(lib_path.c_str(), RTLD_NOW), dlclose)
    , darknet_impl_(LoadDarknetImpl(lib_handle_.get(), job, model_settings))
{

}


DarknetDetection::DarknetDl::darknet_impl_t DarknetDetection::DarknetDl::LoadDarknetImpl(
        void *lib_handle, const MPFJob &job, const ModelSettings &model_settings) {

    if (lib_handle == nullptr) {
        throw std::runtime_error(std::string("Failed to open darknet implementation library: ") + dlerror());
    }

    auto create_impl_fn
            = LoadFunction<DarknetInterface* (const std::map<std::string, std::string>*, const ModelSettings*)>(
                    lib_handle, "darknet_impl_creator");

    auto delete_impl_fn
            = LoadFunction<void (DarknetInterface*)>(
                    lib_handle, "darknet_impl_deleter");

    darknet_impl_t darknet_impl(nullptr, delete_impl_fn);
    try {
        darknet_impl = { create_impl_fn(&job.job_properties, &model_settings), delete_impl_fn };
    }
    catch (const MPFDetectionException &ex) {
        throw ex;
    }
    catch (const std::exception &ex) {
        throw std::runtime_error(std::string(
                "Failed to load darknet implementation library because the darknet_impl_creator function threw an exception: ")
                 + ex.what());
    }

    if (darknet_impl == nullptr) {
        throw std::runtime_error(
                "Failed to load darknet implementation library because the darknet_impl_creator function returned null.");
    }

    return darknet_impl;
}


template<typename TFunc>
TFunc* DarknetDetection::DarknetDl::LoadFunction(void *lib_handle, const char *symbol_name) {
    auto result = reinterpret_cast<TFunc*>(dlsym(lib_handle, symbol_name));
    if (result == nullptr) {
        throw std::runtime_error(std::string("dlsym failed for ") + symbol_name + ": " + dlerror());
    }
    return result;
}

std::vector<DarknetResult> DarknetDetection::DarknetDl::Detect(const cv::Mat &cv_image) {
    return darknet_impl_->Detect(cv_image);
}


MPF_COMPONENT_CREATOR(DarknetDetection);
MPF_COMPONENT_DELETER();
