#include <atomic>
#include <cassert>
#include <chrono>
#include "../CImg.h"
#include <cstdlib>
#include <experimental/filesystem>
#include <ff/farm.hpp>
#include <ff/parallel_for.hpp>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>

using namespace cimg_library;
using namespace ff;
namespace fs = std::experimental::filesystem;

#define IMG_NUM 44
#define W 1024
#define H 768

typedef struct delimiter_t {
    int start;
    int end;
} delimiter;

std::atomic_int PROCESSED_IMAGES = 0;
int REMAINING_IMAGES = 0;
std::mutex IMAGES_MUTEX;

class Emitter: public ff_node {
    public:
        Emitter(int img_size, int par_degree, int workload): img_size(img_size), par_degree(par_degree), \
        workload(workload) {
            start = 0;
            end = (start + workload) - 1;
        }

        void * svc(void *) {
            delimiter * t = (delimiter *) calloc(1, sizeof(delimiter));

            if (par_degree > 0) {
                t -> start = start;
                t -> end = end;

                start = end + 1;
                end = (start + workload) - 1;
                par_degree -= 1;

                return t;
            } else {
                return NULL;
            }
        }

    private:
        int img_size;
        int par_degree;
        int workload;
        int start;
        int end;
};

class Worker: public ff_node {
    public:
        Worker(std::vector<std::string>& images, CImg<unsigned char>& watermark, std::string output_dir): \
        images(images), watermark(watermark), output_dir(output_dir) {}

        void * svc(void * task) {
            delimiter * t = (delimiter *) task;

            ParallelFor pf;
            pf.parallel_for(t -> start, (t -> end) + 1, [=](int i) {
                CImg<unsigned char> img;
                img.assign(images[i].c_str());

                if (!(img.width() != 1024 || img.height() != 768)) {
                    cimg_forXY(watermark, x, y) {
                        int R = (int)watermark(x, y, 0, 0);

                        if (R != 255) {
                            img(x, y, 0, 0) = 0;
                            img(x, y, 0, 1) = 0;
                            img(x, y, 0, 2) = 0;
                        }
                    }

                    std::string fname = images[i].substr(images[i].find_last_of('/') + 1);

                    try {
                        img.save_jpeg(((std::string)output_dir + (std::string)"/" + fname).c_str());
                    } catch (CImgIOException e) {
                        img.save_jpeg(((std::string)output_dir + (std::string)"/" + fname).c_str());
                    }
                    PROCESSED_IMAGES += 1;
                }
                img.clear();
            });

            CImg<unsigned char> img;

            while (true) {
                IMAGES_MUTEX.lock();
                if (REMAINING_IMAGES == 0) {
                    IMAGES_MUTEX.unlock();
                    break;
                } else {
                    int idx = images.size() - REMAINING_IMAGES;
                    REMAINING_IMAGES -= 1;
                    IMAGES_MUTEX.unlock();

                    img.assign(images[idx].c_str());

                    if (!(img.width() != 1024 || img.height() != 768)) {
                        cimg_forXY(watermark, x, y) {
                            int R = (int)watermark(x, y, 0, 0);

                            if (R != 255) {
                                img(x, y, 0, 0) = 0;
                                img(x, y, 0, 1) = 0;
                                img(x, y, 0, 2) = 0;
                            }
                        }

                        std::string fname = images[idx].substr(images[idx].find_last_of('/') + 1);

                        try {
                            img.save_jpeg(((std::string)output_dir + (std::string)"/" + fname).c_str());
                        } catch (CImgIOException e) {
                            img.save_jpeg(((std::string)output_dir + (std::string)"/" + fname).c_str());
                        }
                        PROCESSED_IMAGES += 1;
                    }
                    img.clear();
                }
            }
            return NULL;
        }

    private:
        std::vector<std::string> images;
        CImg<unsigned char> watermark;
        std::string output_dir;
};

int main(int argc, char const *argv[]) {
    auto completion_time_start = std::chrono::high_resolution_clock::now();

    assert((argc == 5) && fs::exists(argv[2]) && fs::exists(argv[1]));

    int par_degree = std::atoi(argv[3]);

    assert(par_degree > 1 && par_degree <= IMG_NUM);

    CImg<unsigned char> watermark(argv[2]);
    std::vector<std::string> images;

    for (auto& path : fs::directory_iterator(argv[1])) {
        std::string fname = path.path().string().substr(path.path().string().find_last_of('/') + 1);

        if (fname != ".DS_Store") {
            images.push_back(path.path().string());
        }
    }

    if (!(fs::exists((std::string)argv[4]))) {
        fs::create_directory((std::string)argv[4]);
    }

    int workload = (int)images.size() / par_degree;
    REMAINING_IMAGES = images.size() - (workload * par_degree);

    ff_farm<> farm;

    Emitter e(images.size(), par_degree, workload);
    farm.add_emitter(&e);

    std::vector<ff_node *> workers;

    for (int i = 0; i < par_degree; i++) {
        workers.push_back(new Worker(std::ref(images), std::ref(watermark), (std::string)argv[4]));
    }

    farm.add_workers(workers);

    if (farm.run_and_wait_end() < 0) {
        return -1;
    }

    auto completion_time_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::ratio<1>> completion_time = completion_time_end - \
                                                                   completion_time_start;

    std::cout << "\nPARALLELISM DEGREE: " << par_degree << std::endl;
    std::cout << "COMPLETION TIME: " << completion_time.count() << " SECONDS" << std::endl;
    // std::cout << "TOTAL OVERHEAD TIME: " << OVERHEAD_TIME << " SECONDS" << std::endl;
    std::cout << "PROCESSED IMAGES: " << PROCESSED_IMAGES << std::endl;

    return 0;
}