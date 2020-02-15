/*
 * SCV - An automatic video analysis tool
 * Copyright (C) 2020  Eli Stone eli.stonium@gmail.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "runner.h"
#include <math.h>
#include <iostream>
#include <sys/stat.h>
#include <limits.h>
#include <map>
#include <time.h>
#include <sys/time.h>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <sstream>
#include <fstream>


#define INBUF_SIZE 4096

void runner::_mkdir(const char *dir) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp),"%s",dir);
    len = strlen(tmp);
    if(tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for(p = tmp + 1; *p; p++) {
        if(*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
        mkdir(tmp, S_IRWXU);
    }
}

void runner::doSimulations(runner::runSettings rs)
{
    auto nextSpeed = [] (int speed, bool altTune, bool fwdKF) -> int {
        int trueSpeed = speed & 31;
        if (trueSpeed > 1) {
            return speed - 1;
        }

        bool fastDeadline = (speed & 65536) == 65536;
        if (fastDeadline) {
            return (5 + 96 + 128);
        }

        int altTuneInt = speed & 96;

        if (fwdKF && ( (speed & 128) == 128)) {
            return (speed + 4 - 128);
        } else if (altTune && altTuneInt > 0) {
            if (fwdKF)
                return (speed + 4 + 128 - 32);
            else
                return (speed + 4 - 32);
        }
        return 0;
    };

    std::ofstream myfile;
    if (rs.outputCSV) {
        std::ifstream testOutputFile(rs.outputCSVFile);
        if (testOutputFile.good()) {
            std::cerr << "Output File: " << rs.outputCSVFile << " already exists. Continue anyway? [y/N]" << std::endl;
            char c;
            std::cin >> c;
            if (c != 'y' && c != 'Y') {
                return;
            }
        }
        myfile.open(rs.outputCSVFile);
        if (rs.useQFactor) {
            myfile << "Test#, Qfac, vmaf, Pass1CTime, Pass2CTime, NetCTime, NetRT, Speed, Tune, FwdKF, RTDeadline, Size";
        } else {
            myfile << "Test#, Bitrate, vmaf, Pass1CTime, Pass2CTime, NetCTime, NetRT, Speed, Tune, FwdKF, RTDeadline, Size";
        }
    }


    std::vector<singleRun> runsList;
    // Get video parameters, decode source, and other initialization


    {
        AVCodecContext *context = NULL;
        AVPacket *pkt;
        pkt = av_packet_alloc();
        AVStream *stream = NULL;
        AVFormatContext *fmt_ctx = NULL;
        int idx = -1;

        _mkdir(rs.temporaryStorageLocation.c_str());

        std::string outfilename = (rs.temporaryStorageLocation + "/rawsource.yuv");

        /* open input file, and allocate format context */
        if (avformat_open_input(&fmt_ctx, rs.inputFile.c_str(), NULL, NULL) < 0) {
            fprintf(stderr, "Could not open source file %s\n", outfilename.c_str());
            exit(1);
        }

        /* retrieve stream information */
        if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
            fprintf(stderr, "Could not find stream information\n");
            exit(1);
        }


        [] (int *stream_idx, AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type) {
            int ret, stream_index;
            AVStream *st;
            AVCodec *dec = NULL;
            AVDictionary *opts = NULL;

            ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
            if (ret < 0) {
                fprintf(stderr, "Could not find %s stream in input file\n",
                        av_get_media_type_string(type));
                return ret;
            } else {
                stream_index = ret;
                st = fmt_ctx->streams[stream_index];

                /* find decoder for the stream */
                dec = avcodec_find_decoder(st->codecpar->codec_id);
                if (!dec) {
                    fprintf(stderr, "Failed to find %s codec\n",
                            av_get_media_type_string(type));
                    return AVERROR(EINVAL);
                }

                /* Allocate a codec context for the decoder */
                *dec_ctx = avcodec_alloc_context3(dec);
                if (!*dec_ctx) {
                    fprintf(stderr, "Failed to allocate the %s codec context\n",
                            av_get_media_type_string(type));
                    return AVERROR(ENOMEM);
                }

                /* Copy codec parameters from input stream to output codec context */
                if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
                    fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                            av_get_media_type_string(type));
                    return ret;
                }

                /* Init the decoders, with or without reference counting */
                av_dict_set(&opts, "refcounted_frames", "0", 0);
                if ((ret = avcodec_open2(*dec_ctx, dec, &opts)) < 0) {
                    fprintf(stderr, "Failed to open %s codec\n",
                            av_get_media_type_string(type));
                    return ret;
                }
                *stream_idx = stream_index;
            }
            return 0;
        }(&idx, &context, fmt_ctx, AVMEDIA_TYPE_VIDEO);
        stream = fmt_ctx->streams[idx];

        rs.videoxRes = context->width;
        rs.videoyRes = context->height;


        std::cout << "Source video resolution is " << rs.videoxRes << "x" << rs.videoyRes << std::endl;
        double aspectRatio = (double) context->width / context->height;
        if (rs.xRes <= 0) {
            rs.xRes = rs.yRes * aspectRatio;
        }
        std::cout << "Testing video resolution is " << rs.xRes << "x" << rs.yRes << std::endl;

        if (!stream) {
            fprintf(stderr, "Could not find video stream in the input, aborting\n");
            exit(1);
        }
        std::cout << "Input framerate is: " << stream->avg_frame_rate.num << "/" << stream->avg_frame_rate.den << ", or " << (double) stream->avg_frame_rate.num/stream->avg_frame_rate.den <<  std::endl;

        if (fmt_ctx->duration_estimation_method == 2) {
            std::cout << "WARNING. ffmpeg was unable to determine the video duration accurately. Consider using a different input format if the duration is wrong." << std::endl;
            std::cout << "The recommended input format is one of: av1, vp9, vp8, h.264, with an mkv or mp4 container." << std::endl;
        }
        rs.videoLength = fmt_ctx->duration / 1000000.0;
        rs.videoSize = fmt_ctx->bit_rate * rs.videoLength / 8;
        rs.videoFrames = (int) (rs.videoLength * ((double) stream->avg_frame_rate.num/stream->avg_frame_rate.den));
        rs.videoFPSNum = stream->avg_frame_rate.num;
        rs.videoFPSDenom = stream->avg_frame_rate.den;
        rs.uncompressedVideoSize = stream->codecpar->color_space * rs.videoFrames * rs.xRes * rs.yRes * 1.5;
        std::cout << "px format is " << stream->codecpar->format << std::endl;
        if (stream->codecpar->format == 0) {
            rs.videoDepth = 8;
        } else if (stream->codecpar->format == 64) {
            // 12 and 10 are the same once encoded to yuv.
            rs.videoDepth = 12;
        } else {
            std::cout << "Unable to determine pixel format of source video. Please use yuv420p and little endian encoding for >8 bits" << std::endl;
            exit(1);
        }


        std::cout << "The input video stream has a duration of " << rs.videoLength << " seconds and a size of " << rs.videoSize / 1024 / 1024 << "MB" << std::endl;
        std::cout << "Converting to raw before running tests... Be sure the destination has the required space" << std::endl;
        std::cout << "Total space needed for testing is roughly: " << 2.1 * rs.uncompressedVideoSize / 1024.0 / 1024.0 << "MB" << std::endl;
        std::cout << "Press any key to start running tests, or ^C to cancel." << std::endl;
        std::getchar();

        std::string ffmpegCmd = "ffmpeg -i '" + rs.inputFile + "' -s " + std::to_string(rs.xRes) + "x" + std::to_string(rs.yRes) +
        " " + outfilename;

        int status = std::system(ffmpegCmd.c_str());

        /*
        if (status != 0) {
            std::cout << "Error running ffmpeg command: " << ffmpegCmd << std::endl;
            remove(outfilename.c_str());
            exit(status);
        }*/
    }


    // Pass 1 encapsulation
    // Pass 1 quickly finds a rough bitrate for the target vmaf we seek by searching for this bitrate
    // on the fastest speed settings
    double optimalRate;
    bool optimalRateFound = false;
    std::cout << "Running fast rate optimization" << std::endl;
    while (!optimalRateFound) {
        double trueTarget = rs.vmafTarget + ((100- rs.vmafTarget) * 0.3);
        double trueEpsilon = 1.0;
        singleRun sr;
        sr.speed = 65536 + 8 + 128 + 96;
        sr.optimizationPassNumber = 1;
        if (!rs.useQFactor) {
            sr.bitrate = getNextTestBitrate(runsList, trueTarget, sr.optimizationPassNumber);
        } else {
            sr.qFactor = getNextTestQFactor(runsList, rs.vmafTarget, sr.optimizationPassNumber);
        }
        runSim(sr, rs, &myfile);
        //std::cout << sr.vmaf << " when run with a bitrate of " << sr.bitrate << std::endl;

        runsList.push_back(sr);
        //std::cout << "Number of runs to analyze: " << runsList.size() << std::endl;

        if (rs.useQFactor) {
            double q = getNextTestQFactor(runsList, rs.vmafTarget, sr.optimizationPassNumber);
            if (std::abs(q - sr.qFactor) <= 1) {
                optimalRate = std::max(q, sr.qFactor);
                optimalRateFound = true;
            }
        } else if (std::abs(sr.vmaf - trueTarget) < trueEpsilon) {
            optimalRateFound = true;
            optimalRate = sr.bitrate;
        }
    }

    // Pass 2 encapsulation
    // Pass 2 will find the optimal speed at a fixed optimalRate
    long optimalSpeed = 65536 + 8 + 128 + 96;
    bool optimalSpeedFound = false;
    if (!rs.useCPUTime && rs.timeCostRatio <= 0) {
        optimalSpeed = 0;
        optimalSpeedFound = true;
    }
    std::cout << "Optimizing for speed." << std::endl;

    while (!optimalSpeedFound) {
        singleRun sr;
        sr.bitrate = optimalRate;
        sr.qFactor = optimalRate;
        sr.optimizationPassNumber = 2;
        sr.speed = optimalSpeed;
        runSim(sr, rs, &myfile);
        runsList.push_back(sr);
        if (rs.targetTimeRatio && optimalSpeed == 0) {
            double fitnessMax = 0;
            int fittestIndex = 0;
            int firstRunIndex = 0;
            double powerUsed = std::log2(rs.targetTimeRatio);

            for (int i = 0; i < runsList.size(); i++) {
                if (runsList.at(i).optimizationPassNumber == 2) {
                    if (firstRunIndex == 0)
                        firstRunIndex = i;
                    double netValue = std::pow(rs.videoSize / runsList.at(firstRunIndex).videoSize, powerUsed);
                    double rawCost;
                    if (rs.useCPUTime) {
                        rawCost = runsList.at(i).netCpuTime / runsList.at(firstRunIndex).netCpuTime;
                    } else {
                        rawCost = runsList.at(i).realTime / runsList.at(firstRunIndex).realTime;
                    }
                    if (netValue / rawCost > fitnessMax) {
                        fitnessMax = netValue / rawCost;
                        fittestIndex = i;
                    }
                }
            }
            optimalSpeed = runsList.at(fittestIndex).speed;
            optimalSpeedFound = true;
        } else if (!rs.targetTimeRatio) {
            if (rs.useCPUTime && (rs.videoLength / sr.netCpuTime) < rs.timescaleTarget / rs.cores) {
                optimalSpeed = runsList.at(runsList.size() - 2).speed;
                optimalSpeedFound = true;
            } else if (!rs.useCPUTime && (rs.videoLength / sr.realTime) < rs.timescaleTarget) {
                optimalSpeed = runsList.at(runsList.size() - 2).speed;
                optimalSpeedFound = true;
            }
        }
        optimalSpeed = nextSpeed(optimalSpeed, rs.testAlternativeTunings, rs.testFwdFrames);
    }

    // Pass 3 finds the exact bitrate and does nothing when q factor is used
    bool exactBitrateFound = false;
    double exactBitrate;
    std::cout << "Finding exact bitrate" << std::endl;
    while (!exactBitrateFound && !rs.useQFactor) {
        singleRun sr;
        sr.speed = optimalSpeed;
        sr.optimizationPassNumber = 3;
        sr.bitrate = getNextTestBitrate(runsList, rs.vmafTarget, sr.optimizationPassNumber, optimalRate);
        runSim(sr, rs, &myfile);
        //std::cout << sr.vmaf << " when run with a bitrate of " << sr.bitrate << std::endl;
        runsList.push_back(sr);

        if (std::abs(sr.vmaf - rs.vmafTarget) < rs.vmafEpsilon) {
            exactBitrateFound = true;
            exactBitrate = sr.bitrate;
        }
    }

    if (rs.outputCSV) {
        myfile.close();
    }

    std::string outfilename = (rs.temporaryStorageLocation + "/rawsource.yuv");
    remove(outfilename.c_str());
    return;
}

double runner::getNextTestBitrate(std::vector<singleRun> &runsList, double target, long passNum, double defaultBR)
{
    std::vector<double> brList;
    std::vector<double> vmafList;
    for (int i = 0; i < runsList.size(); i++) {
        if (runsList.at(i).optimizationPassNumber == passNum) {
            brList.push_back(runsList.at(i).bitrate);
            vmafList.push_back(runsList.at(i).vmaf);
        }
    }
    std::cout << "Number of runs to analyze: " << std::to_string(brList.size()) << std::endl;
    if (brList.size() == 0) {
        return defaultBR;
    }

    bool allLess = true;
    bool allGreater = true;

    int close1Index;
    int close2Index;
    double close1 = 101;
    double close2 = 101;
    for (int i = 0; i < vmafList.size(); i++){
        if (vmafList.at(i) > target) {
            allLess = false;
        } else {
            allGreater = false;
        }
        double vmafDiff = std::abs(vmafList.at(i) - target);
        if (close1 > vmafDiff) {
            close2 = close1;
            close2Index = close1Index;
            close1 = vmafDiff;
            close1Index = i;
        } else if (close2 > vmafDiff) {
            close2 = vmafDiff;
            close2Index = i;
        }
    }
    if (allLess) {
        return brList.at(vmafList.size() - 1) * 2.0;
    } else if (allGreater) {
        return brList.at(vmafList.size() - 1) * 0.5;
    }
    // if not get the middle between the two closest results.
    return (brList.at(close1Index) + brList.at(close2Index)) / 2.0;
}

double runner::getNextTestQFactor(std::vector<singleRun> &runsList, double target, long passNum, double defaultQ)
{
    std::vector<double> qList;
    std::vector<double> vmafList;
    for (int i = 0; i < runsList.size(); i++) {
        if (runsList.at(i).optimizationPassNumber == passNum) {
            qList.push_back(runsList.at(i).qFactor);
            vmafList.push_back(runsList.at(i).vmaf);
        }
    }
    if (qList.size() == 0) {
        return defaultQ;
    }

    bool allLess = true;
    bool allGreater = true;

    int close1Index;
    int close2Index;
    double close1 = 101;
    double close2 = 101;
    for (int i = 0; i < vmafList.size(); i++){
        if (vmafList.at(i) > target) {
            allLess = false;
        } else {
            allGreater = false;
        }
        double vmafDiff = std::abs(vmafList.at(i) - target);
        if (close1 > vmafDiff) {
            close2 = close1;
            close2Index = close1Index;
            close1 = vmafDiff;
            close1Index = i;
        } else if (close2 > vmafDiff) {
            close2 = vmafDiff;
            close2Index = i;
        }
    }
    if (allLess) {
        if (qList.at(vmafList.size() - 1) - 8 < 0) {
            return 0;
        } else {
            return (int) (qList.at(vmafList.size() - 1) - 8);
        }
    } else if (allGreater) {
        if (qList.at(vmafList.size() - 1) + 8 < 68)
            return (int) (qList.at(vmafList.size() - 1) + 8);
        else {
            return 68;
        }
    }
    // if not get the middle between the two closest results.
    return (int) ((qList.at(close1Index) + qList.at(close2Index)) / 2.0);
}


void runner::runSim(runner::singleRun& sr, runner::runSettings rs, std::ofstream *myfile)
{
    bool twoRuns = ( (sr.speed & 65536) == 0 && rs.useTwoPass);
    auto exec = [] (const char* cmd) {
        std::array<char, 128> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
        if (!pipe) {
            throw std::runtime_error("popen() failed!");
        }
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        return result;
    };

    auto walltime = [] () -> double {
        struct timeval time;
        if (gettimeofday(&time,NULL)){
            //  Handle error
            return 0.0;
        }
        return (double)time.tv_sec + (double)time.tv_usec * .000001;
    };

    auto explainstring = [] (runner::singleRun& sr) -> std::string {
        std::string e = "";
        if ( (sr.speed & 65536) == 0) {
            e += "Using 'good' deadline";
        } else
            e += "Using 'rt' deadline";
        switch (sr.speed & 96) {
            case 0:
                e += "\nUsing vmaf_with_preprocessing tuning";
                break;
            case 32:
                e += "\nUsing vmaf_without_preprocessing tuning";
                break;
            case 64:
                e += "\nUsing ssim tuning";
                break;
            case 96:
                e += "\nUsing psnr tuning";
                break;
        }
        if ( (sr.speed & 128) != 128) {
            e += "\nUsing forward keyframes";
        } else {
            e += "\nNot using forward keyframes";
        }

        e += "\nUsing cpu speed: " + std::to_string((sr.speed & 31));
        return e;
    };

    auto cmdstring = [] (runner::singleRun& sr, runner::runSettings rs, int runNumber = 2) -> std::string {
        std::string cmd = "aomenc";

        cmd += " --bit-depth=" + std::to_string(rs.bits) + " --width=" + std::to_string(rs.xRes) + " --height=" + std::to_string(rs.yRes) + " --fps=" + std::to_string(rs.videoFPSNum) + "/" + std::to_string(rs.videoFPSDenom);

        if (runNumber != 0)
            cmd += " --passes=2 --pass=" + std::to_string(runNumber);
        else
            cmd += " --passes=1 --pass=1";
        cmd += " --input-bit-depth=" + std::to_string(rs.videoDepth);

        if ( (sr.speed & 65536) != 0)
            cmd += " --rt";
        else
            cmd += " --good";


        if (rs.useQFactor)
            cmd += " --end-usage=cq --cq-level=" + std::to_string(sr.qFactor);
        else
            cmd += " --end-usage=vbr --bias-pct=100 --target-bitrate=" + std::to_string((int) sr.bitrate);
        if (rs.useQFactor && sr.qFactor == 0)
            cmd += " --lossless=1";



        int truespeed = sr.speed & 31;
        cmd += " --cpu-used=" + std::to_string(truespeed);

        bool forwardKF = ! ((sr.speed & 128) == 128);
        int tuning = sr.speed & 96;
        tuning = tuning / 32;
        switch(tuning) {
            case 0:
                cmd += " --tune=vmaf_with_preprocessing";
                break;
            case 1:
                cmd += " --tune=vmaf_without_preprocessing";
                break;
            case 2:
                cmd += " --tune=ssim";
                break;
            case 3:
                cmd += " --tune=psnr";
                break;
            default:
                break;
        }
        // One every 10 seconds.
        int keyframeDistance = (int) (rs.videoFrames / rs.videoLength * 10.0);

        if (forwardKF)
            cmd += " --enable-fwd-kf=1 --kf-max-dist=" + std::to_string(keyframeDistance);
        else
            cmd += " --enable-fwd-kf=0 --kf-max-dist=" + std::to_string(keyframeDistance);

        cmd += " --ivf --output='" + rs.temporaryStorageLocation + "/output.ivf'" + " '" + rs.temporaryStorageLocation + "/rawsource.yuv'";

        return cmd;
    };

    std::string readPath = "/proc/self/stat";

    {
        std::string token;
        std::stringstream buf, buf2;
        double cpuT1 = 0;

        std::ifstream t1(readPath);
        buf << t1.rdbuf();
        for (int i = 0; i < 17; i++) {
            buf >> token;
            if (i >= 15) {
                cpuT1 += atof(token.c_str());
            }
        }

        int rn = twoRuns;
        std::string cmd = cmdstring(sr, rs, rn);
        //std::cout << explainstring(sr) << std::endl;
        //std::cout << "Running command:\n" << cmd << std::endl;

        double startRT = walltime();

        if (system(cmd.c_str()) != 0) {
            std::cout << "Error running aomenc, exiting." << std::endl;
            exit(1);
        }
        double endRT = walltime();
        double cpuT2 = 0;

        std::ifstream t2(readPath);
        buf2 << t2.rdbuf();
        for (int i = 0; i < 17; i++) {
            buf2 >> token;
            if (i >= 15) {
                cpuT2 += atof(token.c_str());
            }
        }


        sr.realTime = endRT - startRT;
        sr.cpuTimeP1 = (cpuT2 - cpuT1) / sysconf(_SC_CLK_TCK);

        //std::cout << "Completed first pass aomenc run at speed " << sr.speed << std::endl;
        //std::cout << "Starttime is " << (double) cpuT1/sysconf(_SC_CLK_TCK) << " end time is " << (double) cpuT2 / sysconf(_SC_CLK_TCK) << ". diff is " << (double) (cpuT2 - cpuT1) / sysconf(_SC_CLK_TCK) << std::endl;
        //std::cout << "Starttime RT is " << startRT << " end time RT is " << endRT << ". diff is " << endRT - startRT << std::endl;
    }

    if (!twoRuns) {
        sr.cpuTimeP2 = 0;
        sr.netCpuTime = sr.cpuTimeP1;

    } else {
        std::string token;
        std::stringstream buf, buf2;
        double cpuT1 = 0;

        std::ifstream t1(readPath);
        buf << t1.rdbuf();
        for (int i = 0; i < 17; i++) {
            buf >> token;
            if (i >= 15) {
                cpuT1 += atof(token.c_str());
            }
        }



        double startRT = walltime();

        std::string cmd = cmdstring(sr, rs);
        //std::cout << explainstring(sr) << std::endl;
        //std::cout << "Running command:\n" << cmd << std::endl;

        if (system(cmd.c_str()) != 0) {
            std::cout << "Error running aomenc, exiting." << std::endl;
            exit(1);
        }
        double endRT = walltime();

        double cpuT2 = 0;

        std::ifstream t2(readPath);
        buf2 << t2.rdbuf();
        for (int i = 0; i < 17; i++) {
            buf2 >> token;
            if (i >= 15) {
                cpuT2 += atof(token.c_str());
            }
        }

        sr.realTime = sr.realTime + endRT - startRT;
        sr.cpuTimeP2 = (cpuT2 - cpuT1) / sysconf(_SC_CLK_TCK);
        sr.netCpuTime = sr.cpuTimeP1 + sr.cpuTimeP2;
        //std::cout << "Completed second pass aomenc run at speed " << sr.speed << std::endl;
        //std::cout << "Starttime is " << (double) cpuT1/sysconf(_SC_CLK_TCK) << " end time is " << (double) cpuT2 / sysconf(_SC_CLK_TCK) << ". diff is " << (double) (cpuT2 - cpuT1) / sysconf(_SC_CLK_TCK) << std::endl;
        //std::cout << "Starttime RT is " << startRT << " end time RT is " << endRT << ". diff is " << endRT - startRT << std::endl;

    }
    std::string f1 = rs.temporaryStorageLocation + "/rawoutput.yuv";
    std::string f2 = rs.temporaryStorageLocation + "/output.ivf";

    struct stat filestatus;
    stat(f2.c_str(), &filestatus );
    sr.videoSize = filestatus.st_size;

    std::string ffmpegCmd = "ffmpeg -i '" + f2 + "' -s " + std::to_string(rs.xRes) + "x" + std::to_string(rs.yRes) +
    " '" + f1 + "'";


    if (system(ffmpegCmd.c_str()) != 0) {
        std::cout << "Unable to convert output video to raw format" << std::endl;
        exit(1);
    }

    std::stringstream buffer;
    std::streambuf * old = std::cerr.rdbuf(buffer.rdbuf());

    std::string vmafCmd = "vmafossexec yuv420p " + std::to_string(rs.xRes) + " " + std::to_string(rs.yRes) + " '" + rs.temporaryStorageLocation + "/rawsource.yuv' '" + f1 + "' '" + rs.vmafModel + "'";

    std::string vmafOut = exec(vmafCmd.c_str());

    std::size_t found = vmafOut.find("VMAF score = ");
    found += 13;
    std::string vmafVal = vmafOut.substr(found, vmafOut.size() - found);
    sr.vmaf = std::atof(vmafVal.c_str());

    int trueSpeed = sr.speed & 31;
    bool fastDeadline = (sr.speed & 65536) == 65536;
    int altTuneInt = sr.speed & 96;
    std::string altTune = "";
    switch (altTuneInt) {
        case 0:
            altTune = "vmaf_with_preprocessing";
            break;
        case 32:
            altTune = "vmaf_without_preprocessing";
            break;
        case 64:
            altTune = "ssim";
            break;
        case 96:
            altTune = "psnr";
            break;
    }
    bool fwdKF = (sr.speed & 128) != 128;

    std::cout << "Results for run are:" << std::endl;
    if (rs.useQFactor) {
        std::cout << "Test#, Qfac, vmaf, Pass1CTime, Pass2CTime, NetCTime, NetRT, Speed, Tune, FwdKF, RTDeadline, Size" << std::endl;
        if (rs.outputCSV)
            *myfile << std::endl << sr.optimizationPassNumber << ", " << sr.qFactor << ", " << sr.vmaf << ", " << sr.cpuTimeP1 << ", " << sr.cpuTimeP2 << ", " << sr.netCpuTime << ", " << sr.realTime << ", " << trueSpeed << ", " << altTune << ", " << fwdKF << ", " << fastDeadline << sr.videoSize;

        std::cout << sr.optimizationPassNumber << ", " << sr.qFactor << ", " << sr.vmaf << ", " << sr.cpuTimeP1 << ", " << sr.cpuTimeP2 << ", " << sr.netCpuTime << ", " << sr.realTime << ", " << trueSpeed << ", " << altTune << ", " << fwdKF << ", " << fastDeadline << sr.videoSize << std::endl;
    } else {
        std::cout << "Test#, Bitrate, vmaf, Pass1CTime, Pass2CTime, NetCTime, NetRT, Speed, Tune, FwdKF, RTDeadline, Size" << std::endl;

        if (rs.outputCSV)
            *myfile << std::endl << sr.optimizationPassNumber <<  ", " << sr.bitrate << ", " << sr.vmaf << ", " << sr.cpuTimeP1 << ", " << sr.cpuTimeP2 << ", " << sr.netCpuTime << ", " << sr.realTime << ", " << trueSpeed << ", " << altTune << ", " << fwdKF << ", " << fastDeadline << sr.videoSize;

        std::cout << sr.optimizationPassNumber <<  ", " << sr.bitrate << ", " << sr.vmaf << ", " << sr.cpuTimeP1 << ", " << sr.cpuTimeP2 << ", " << sr.netCpuTime << ", " << sr.realTime << ", " << trueSpeed << ", " << altTune << ", " << fwdKF << ", " << fastDeadline << sr.videoSize << std::endl;
    }


    if (remove(f1.c_str()) != 0) {
        std::cout << "Error removing " << f1 << std::endl;
    }
    if (remove(f2.c_str()) != 0) {
        std::cout << "Error removing " << f1 << std::endl;
    }
}





