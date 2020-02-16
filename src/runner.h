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

#pragma once

#include <string>
#include <vector>
extern "C" {
    #include <libavutil/imgutils.h>
    #include <libavutil/samplefmt.h>
    #include <libavutil/timestamp.h>
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
}


/**
 * @todo write docs
 */
namespace runner
{
    struct runSettings {
        std::string temporaryStorageLocation = "/tmp/scv";
        std::string inputFile;
        std::string outputCSVFile = "";
        std::string encodingProgram = "aomenc";
        std::string vmafModel = "/usr/share/model/vmaf_v0.6.1.pkl";
        double vmafTarget = 95;
        double vmafEpsilon = 0.05;
        double timeCostRatio = 10;
        double timescaleTarget = 0.01;
        double cores = 1.0;
        bool outputCSV = false;
        bool useCPUTime = true;
        bool targetTimeRatio = false;
        bool useQFactor = false;
        bool useTwoPass = true;
        bool testAlternativeTunings = false;
        bool testFwdFrames = false;
        int bits = 8;
        int xRes = 0;
        int yRes = 0;
        int videoxRes = 1;
        int videoyRes = 1;
        int videoFPSNum = 1;
        int videoFPSDenom = 1;
        double videoLength = 0.001;
        long videoFrames = 1;
        long videoSize = 4096;
        long uncompressedVideoSize = 4096;
        int videoDepth = 8;
    };

    struct singleRun {
        long optimizationPassNumber;
        double bitrate;
        double qFactor;
        long speed;
        double realTime;
        double cpuTimeP1;
        double cpuTimeP2;
        double netCpuTime;
        double vmaf;
        long videoSize;
    };
    void doSimulations(runSettings rs);

    double getNextTestBitrate(std::vector<singleRun> &runsList, double target, long passNum, double defaultBR = 10000);
    double getNextTestQFactor(std::vector<singleRun> &runsList, double target, long passNum, double defaultQ = 30);
    void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt,
                const char *filename);
    std::string runSim(singleRun& sr, runSettings rs, std::ofstream *myfile = nullptr);

    void _mkdir(const char *dir);
};
