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


#include "DarknetImpl.h"

#include <chrono>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#ifdef GPU
#include <cuda_runtime_api.h>
#endif

#include <detectionComponentUtils.h>
#include <MPFDetectionException.h>
#include <MPFInvalidPropertyException.h>
#include <Utils.h>



using namespace MPF::COMPONENT;


namespace DarknetHelpers {

    ProbHolder::ProbHolder(int output_layer_size, int num_classes)
        // The make_probs function from the Darknet library allocates num_classes + 1 floats per row.
        // There is no documentation so it isn't clear why it uses num_classes + 1 instead of just num_classes.
        : mat_size_(output_layer_size * (num_classes + 1))
        , prob_mat_(new float[mat_size_]())
        , prob_row_ptrs_(new float*[output_layer_size])
    {
        for (int i = 0; i < output_layer_size; i++) {
            prob_row_ptrs_[i] = &prob_mat_[i * (num_classes + 1)];
        }
    }

    float** ProbHolder::Get() {
        return prob_row_ptrs_.get();
    }

    float* ProbHolder::operator[](size_t box_idx) {
        return prob_row_ptrs_[box_idx];
    }

    void ProbHolder::Clear() {
        for (int i = 0; i < mat_size_; i++) {
            prob_mat_[i] = 0.0f;
        }
    }


    // Darknet uses its own image type, which is a C struct. This class adds two features to the Darknet image type.
    // One is that it adds a destructor, which calls the free_image function defined in the Darknet library.
    // The second feature is that it converts a cv::Mat to the Darknet image format.
    struct DarknetImageHolder {
        int frame_number;
        cv::Size original_size;
        image darknet_image;

        DarknetImageHolder(int frame_number, const cv::Mat &cv_image, const cv::Size &target_size)
                : frame_number(frame_number)
                , original_size(cv_image.size())
                , darknet_image(CvMatToImage(cv_image, target_size))
        {
        }

        ~DarknetImageHolder() {
            free_image(darknet_image);
        }

        DarknetImageHolder(const DarknetImageHolder&) = delete;
        DarknetImageHolder& operator=(const DarknetImageHolder&) = delete;
        DarknetImageHolder(DarknetImageHolder&&) = delete;
        DarknetImageHolder& operator=(DarknetImageHolder&&) = delete;


        static image CvMatToImage(const cv::Mat &cv_image, const cv::Size &target_size) {
            // Darknet uses its own image type, which is a C struct.
            image tmp_image = make_image(cv_image.cols,
                                         cv_image.rows,
                                         cv_image.channels());

            // This code is mostly copied from Darknet's "ipl_into_image"
            // function. The main difference is that this function works
            // with cv::Mat instead of IplImage. IplImage is a legacy
            // OpenCV image type. The OpenCV documentation for cv::Mat
            // says that cv::Mat and IplImage use a compatible data
            // layout.
            int width = tmp_image.w;
            int height = tmp_image.h;
            int channels = cv_image.channels();
            size_t step = cv_image.step[0];

            for (int row = 0; row < height; row++) {
                for (int col = 0; col < width; col++) {
                    for (int channel = 0; channel < channels; channel++) {
                        tmp_image.data[channel * width * height + row * width + col]
                                = cv_image.data[row * step + col * channels + channel] / 255.0f;
                    }
                }
            }
            rgbgr_image(tmp_image);
            image darknet_image = letterbox_image(tmp_image, target_size.width, target_size.height);
            free_image(tmp_image);
            return darknet_image;
        }
    };
} // end of DarknetHelpers namespace


namespace {
    // Darknet functions that accept C style strings, accept a char* instead of a const char*.
    std::unique_ptr<char[]> ToNonConstCStr(const std::string &str) {
        std::unique_ptr<char[]> result(new char[str.size() + 1]);
        size_t length = str.copy(result.get(), str.size());
        result[length] = '\0';
        return result;
    }


    DarknetHelpers::network_ptr_t LoadNetwork(const ModelSettings &model_settings) {
        auto cfg_file = ToNonConstCStr(model_settings.network_config_file);
        auto weights_file = ToNonConstCStr(model_settings.weights_file);

        return { load_network(cfg_file.get(), weights_file.get(), 0),
                 free_network };
    }


    int GetOutputLayerSize(const network &network) {
        layer output_layer = network.layers[network.n - 1];
        return output_layer.w * output_layer.h * output_layer.n;
    }


    int GetNumClasses(const network &network) {
        layer output_layer = network.layers[network.n - 1];
        return output_layer.classes;
    }

    std::vector<std::string> LoadNames(const ModelSettings &model_settings, int expected_name_count) {
        std::ifstream in_file(model_settings.names_file);
        if (!in_file.good()) {
            throw std::runtime_error("Failed to open names file at: " + model_settings.names_file);
        }

        std::vector<std::string> names;
        names.reserve(static_cast<size_t>(expected_name_count));

        std::string line;
        while (std::getline(in_file, line).good()) {
            names.push_back(line);
        }

        if (names.size() != expected_name_count) {
            std::stringstream error;
            error << "Error: The network config file at " << model_settings.network_config_file << " specifies "
                  << expected_name_count << " classes, but the names file at " << model_settings.names_file << " contains "
                  << names.size() << " classes. This is probably because given names file does not correspond to the "
                  << "given network configuration file.";
            throw std::runtime_error(error.str());
        }

        return names;
    }


    cv::Rect BoxToRect(const box &box, const cv::Size &image_size) {
        // box.x and box.y refer to the center of the rectangle,
        // but cv::Rect uses the top left x and y coordinates.
        auto tlX = static_cast<int>(box.x - box.w / 2.0f);
        auto tlY = static_cast<int>(box.y - box.h / 2.0f);
        auto width = static_cast<int>(box.w);
        auto height = static_cast<int>(box.h);

        return cv::Rect(tlX, tlY, width, height) & cv::Rect(cv::Point(0, 0), image_size);
    }


    bool HasWhitelist(const Properties &props) {
        return !DetectionComponentUtils::GetProperty(props, "CLASS_WHITELIST_FILE", std::string())
                    .empty();
    }


    class NoOpFilter {
    public:
        NoOpFilter(const std::map<std::string, std::string> &, const std::vector<std::string>&) { }

        bool operator()(const std::string &) { return true; }
    };


    class WhitelistFilter {
    public:

        WhitelistFilter(const Properties& props, const std::vector<std::string>& names) {
            const std::string &whitelist_path = props.at("CLASS_WHITELIST_FILE");
            std::string expanded_file_path;
            std::string error = Utils::expandFileName(whitelist_path, expanded_file_path);
            if (!error.empty()) {
                throw MPFInvalidPropertyException(
                        "CLASS_WHITELIST_FILE",
                        "The value, \"" + whitelist_path + "\", could not be expanded due to: " + error);
            }

            std::ifstream whitelist_file(expanded_file_path);
            if (!whitelist_file.good()) {
                throw MPFDetectionException(
                        MPF_COULD_NOT_OPEN_DATAFILE,
                        "Failed to load class whitelist that was supposed to be located at \""
                        + expanded_file_path + "\".");
            }

            std::unordered_set<std::string> temp_whitelist;
            std::string line;
            while (std::getline(whitelist_file, line)) {
                Utils::trim(line);
                if (!line.empty()) {
                    temp_whitelist.insert(line);
                }
            }

            if (temp_whitelist.empty()) {
                throw MPFDetectionException(
                        MPF_COULD_NOT_READ_DATAFILE,
                        "The class whitelist file located at \"" + expanded_file_path + "\" was empty.");
            }

            for (const std::string &name : names) {
                if (temp_whitelist.count(name) > 0) {
                    whitelist_.insert(name);
                }
            }

            if (whitelist_.empty()) {
                throw MPFDetectionException(
                        MPF_COULD_NOT_READ_DATAFILE,
                        "None of the class names specified in the whitelist file located at \""
                        + expanded_file_path + "\" were found in the names file.");
            }
        }

        bool operator()(const std::string& class_name) {
            return whitelist_.count(class_name) > 0;
        }

    private:
        std::unordered_set<std::string> whitelist_;

    };
} // end of anonymous namespace


template<typename ClassFilter>
DarknetImpl<ClassFilter>::DarknetImpl(const std::map<std::string, std::string> &props, const ModelSettings &settings)
    : DarknetInterface(props, settings)
    , network_(LoadNetwork(settings))
    , output_layer_size_(GetOutputLayerSize(*network_))
    , num_classes_(GetNumClasses(*network_))
    , names_(LoadNames(settings, num_classes_))
    , class_filter_(props, names_)
    // Darknet will output of a probability for every possible class regardless of the content of the image.
    // Most of these classes will have a probability of zero or a number very close to zero.
    // If the confidence threshold is zero or smaller it will report every possible classification.
    , confidence_threshold_(DetectionComponentUtils::GetProperty(props, "CONFIDENCE_THRESHOLD", 0.5f))
    , probs_(output_layer_size_, num_classes_)
    , boxes_(new box[output_layer_size_])
{

}


template<typename ClassFilter>
std::vector<DarknetResult> DarknetImpl<ClassFilter>::Detect(int frame_number, const cv::Mat &cv_image) {
    std::vector<DarknetResult> detections;
    Detect(frame_number, cv_image, detections);
    return detections;
}

template<typename ClassFilter>
void DarknetImpl<ClassFilter>::Detect(int frame_number, const cv::Mat &cv_image,
                                      std::vector<DarknetResult> &detections) {
    Detect(DarknetHelpers::DarknetImageHolder(frame_number, cv_image, GetTargetFrameSize()), detections);
}


template<typename ClassFilter>
void DarknetImpl<ClassFilter>::Detect(const DarknetHelpers::DarknetImageHolder &image_holder,
                                      std::vector<DarknetResult> &detections) {
    probs_.Clear();

    // There is no documentation explaining what hier_thresh and nms do,
    // so we are just using the default values from the Darknet library.
    float hier_thresh = 0.5;
    float nms = 0.3;
    image im = image_holder.darknet_image;
    set_batch_network(network_.get(), 1);
    network_predict(network_.get(), im.data);

    layer l = network_->layers[network_->n-1];
    if(l.type == REGION){
        int image_width = image_holder.original_size.width;
        int image_height = image_holder.original_size.height;
        get_region_boxes(l, image_width, image_height, network_->w, network_->h,
                         confidence_threshold_, probs_.Get(),
                         boxes_.get(), nullptr, 0, nullptr, hier_thresh, 0);
        do_nms_sort(boxes_.get(), probs_.Get(), l.w*l.h*l.n, l.classes, nms);
    }


    for (int box_idx = 0; box_idx < output_layer_size_; box_idx++) {
        DarknetResult detection(image_holder.frame_number, BoxToRect(boxes_[box_idx], image_holder.original_size));

        for (int name_idx = 0; name_idx < num_classes_; name_idx++) {
            float prob = probs_[box_idx][name_idx];
            if (prob >= confidence_threshold_ && class_filter_(names_[name_idx])) {
                detection.object_type_probs.emplace_back(prob, names_[name_idx]);
            }
        }

        if (!detection.object_type_probs.empty()) {
            detections.push_back(std::move(detection));
        }
    }
}


template<typename ClassFilter>
cv::Size DarknetImpl<ClassFilter>::GetTargetFrameSize() {
    return cv::Size(network_->w, network_->h);
}




DarknetAsyncImpl::DarknetAsyncImpl(const Properties &props, const ModelSettings &settings)
    : DarknetAsyncInterface(props, settings)
    , work_queue_(DetectionComponentUtils::GetProperty(props, "FRAME_QUEUE_CAPACITY", 4))
{
    if (HasWhitelist(props)) {
        Init<WhitelistFilter>(props, settings);
    }
    else {
        Init<NoOpFilter>(props, settings);
    }
}

DarknetAsyncImpl::~DarknetAsyncImpl() {
    // In the normal case, the thread running ProcessFrameQueue will have already exited at this point,
    // so calling halt has no effect.
    // If the thread running ProcessFrameQueue is still active, that indicates an error. Calling halt here will cause
    // the thread running ProcessFrameQueue to exit the next time it tries to access the queue.
    work_queue_.halt();
}


template<typename ClassFilter>
void DarknetAsyncImpl::Init(const Properties &props, const ModelSettings &settings) {
    DarknetImpl<ClassFilter> darknet_impl(props, settings);
    target_frame_size_ = darknet_impl.GetTargetFrameSize();
    work_done_future_ = std::async(std::launch::async,
                                   ProcessFrameQueue<ClassFilter>, std::move(darknet_impl), std::ref(work_queue_));
}


void DarknetAsyncImpl::Submit(int frame_number, const cv::Mat &cv_image) {
    work_queue_.emplace(new DarknetHelpers::DarknetImageHolder(frame_number, cv_image, target_frame_size_));
}


std::vector<DarknetResult> DarknetAsyncImpl::GetResults() {
    if (get_results_called_) {
        // std::future becomes invalid after the first time std::future::get() is called
        throw std::runtime_error("DarknetAsyncImpl::GetResults() can only be called once.");
    }
    get_results_called_ = true;

    try {
        // Put a nullptr into the queue to tell the consumer it is done.
        work_queue_.emplace(nullptr);
    }
    catch (const QueueHaltedException&) {
        bool is_ready_now = std::future_status::ready == work_done_future_.wait_for(std::chrono::milliseconds(0));
        if (!is_ready_now) {
            throw;
        }
    }
    return work_done_future_.get();
}



template<typename ClassFilter>
std::vector<DarknetResult> DarknetAsyncImpl::ProcessFrameQueue(DarknetImpl<ClassFilter> darknet_impl,
                                                               DarknetQueue &work_queue) {
    std::vector<DarknetResult> results;
    try {
        while (true) {
            auto darknet_image = work_queue.pop();
            if (darknet_image == nullptr) {
                break;
            }
            darknet_impl.Detect(*darknet_image, results);
        }
        // No more items will be removed from the work queue at this point.
        // Calling halt here makes sure an exception is thrown if more items are inserted into the queue.
        work_queue.halt();
        return results;
    }
    catch (const QueueHaltedException&) {
        // Other side requested early exit.
        return results;
    }
    catch (...) {
        work_queue.halt();
        throw; // Exception will be re-thrown when work_done_future_.get() is called
    }
}



void configure_cuda_device(const Properties &job_props) {
#ifdef GPU
    int cuda_device_id = DetectionComponentUtils::GetProperty(job_props, "CUDA_DEVICE_ID", -1);
    if (cuda_device_id < 0) {
        throw MPFDetectionException(
                MPF_GPU_ERROR, "CUDA version of darknet library loaded, but the CUDA_DEVICE_ID was not set.");
    }

    gpu_index = cuda_device_id;
    cudaError_t rc = cudaSetDevice(cuda_device_id);
    if (rc != cudaSuccess) {
        throw MPFDetectionException(
                MPF_GPU_ERROR,
                "Failed to set CUDA device to device number " + std::to_string(cuda_device_id)
                         + " due to: " + cudaGetErrorString(rc));
    }
    // Through testing we have determined that the following function
    // must be called after cudaSetDevice() in order for it to take
    // effect on the device just selected. This seems contrary to what
    // is implied by the documentation, specifically regarding calling
    // it before the runtime and driver have been initialized.
    // In addition, our testing has been unable to find a circumstance
    // where this function fails, also despite what the documentation
    // says. For this reason, we treat failure of this function as a
    // fatal error, since it should not fail under normal operation.
    rc = cudaSetDeviceFlags(cudaDeviceBlockingSync);
    if (rc != cudaSuccess) {
        throw MPFDetectionException(MPF_GPU_ERROR, "Could not set CUDA device " + std::to_string(cuda_device_id) + " to use blocking synchronization: " + cudaGetErrorString(rc));
    }
#endif
}


extern "C" {
    DarknetInterface* darknet_impl_creator(const std::map<std::string, std::string> *props,
                                           const ModelSettings *settings) {
        configure_cuda_device(*props);
        if (HasWhitelist(*props)) {
            return new DarknetImpl<WhitelistFilter>(*props, *settings);
        }
        else {
            return new DarknetImpl<NoOpFilter>(*props, *settings);
        }
    }

    void darknet_impl_deleter(DarknetInterface *impl) {
        delete impl;
    }


    DarknetAsyncInterface* darknet_async_impl_creator(const std::map<std::string, std::string> *props,
                                                      const ModelSettings *settings) {
        configure_cuda_device(*props);
        return new DarknetAsyncImpl(*props, *settings);
    }

    void darknet_async_impl_deleter(DarknetAsyncInterface *impl) {
        delete impl;
    }
}
