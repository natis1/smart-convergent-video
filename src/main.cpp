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

#include <iostream>
#include <getopt.h>
#include "runner.h"

void printHelpMenu() {
    std::cout << "Smart Convergent Video - SCV options" << std::endl;
    std::cout << "Usage:\tscv -i filename" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << " -i file\tInput video file" << std::endl;
    std::cout << " -V file\tInput VMAF model. (defaults to /usr/share/model/vmaf_v0.6.1.pkl)" << std::endl;
    std::cout << " -o folder\tTemporary storage location" << std::endl;

    std::cout << " -O file\tOutput to csv file" << std::endl;
    std::cout << " -t timescale\tSolve for ideal compiler settings by solving for a desired encoder timescale\n(target number of seconds of video to encode for every second of execution time)" << std::endl;
    std::cout << " -T value\tSolve for ideal 'value' of compression.\nEG -T10 will think it's worth spending ten times as long to decrease output video size by a factor of 2" << std::endl;
    std::cout << "Setting -T to a negative value, eg -1, will instruct the encoder to use the slowest speed settings and avoid optimizing for time used." << std::endl;
    std::cout << " -p\t\tMeasure time using realtime rather than cpu time. This is not recommended as CPU time is a more useful metric for encoding performance as encoding is highly parallelizable." << std::endl;
    std::cout << " -P value\tExtrapolate the total system performance when finding timescale given value cores." << std::endl;
    std::cout << "As performance may not scale linearly, this can be a decimal value.\n" << std::endl;

    std::cout << " -q value\tTarget VMAF of the output video (ranges from 0-100)" << std::endl;
    std::cout << " -Q value\tAcceptable VMAF deviation from target for output video. Defaults to 0.05" << std::endl;
    std::cout << "Setting -Q to a negative value uses q factor instead of bitrate. Not recommended.\n" << std::endl;
    std::cout << " -M file\tModel file to use for VMAF calculation. Be sure it's appropriate for your video resolution." << std::endl;
    std::cout << " -y value\tRescale the video to a height when testing VMAF. (defaults to 720, use 0 to disable any rescaling)." << std::endl;
    std::cout << " -x value\tRescale the video to a width when testing VMAF. (defaults to preserving the aspect ratio)." << std::endl;
    std::cout << " -0\t\tOutput to and test with 10 bit video. Uses the yuv420p10le format." << std::endl;
    std::cout << " -2\t\tOutput to and test with 12 bit video. Uses the yuv420p12le format." << std::endl;
    std::cout << " -k\tTest speed impact of forward keyframes (experimental)" << std::endl;
    std::cout << " -K\tTest speed impact of alternative tunings (experimental)" << std::endl;

    std::cout << " -n\t\tDo not use 2 pass (VERY NOT RECOMMENDED) for encoding." << std::endl;



}

double getDouble(char* str, double def = 0.0)
{
    char* endptr;
    double value = strtod(str, &endptr);
    if (*endptr || endptr == str) return def;
    return value;
}


int main(int argc, char **argv) {
    struct runner::runSettings rs;
    int opt;

    while((opt = getopt(argc, argv, ":V:i:o:t:T:q:Q:O:x:y:02pnhkKP:")) != -1){ //get option from the getopt() method
        switch(opt){

            //For option i, r, l, print that these are options
            case 'i':
                rs.inputFile = optarg;
                break;
            case 'o':
                rs.temporaryStorageLocation = optarg;
                break;
            case 'V':
                rs.vmafModel = optarg;
                break;
            case 't':
                rs.timescaleTarget = getDouble(optarg, rs.timescaleTarget);
                break;
            case 'T':
                rs.targetTimeRatio = true;
                rs.timeCostRatio = getDouble(optarg, rs.timeCostRatio);
                break;
            case 'q':
                rs.vmafTarget = getDouble(optarg, rs.vmafTarget);
                break;
            case 'Q':
                rs.vmafEpsilon = getDouble(optarg, rs.vmafEpsilon);
                break;
            case 'O':
                rs.outputCSV = true;
                rs.outputCSVFile = optarg;
                break;
            case 'x':
                rs.xRes = (int) getDouble(optarg, (double) rs.xRes);
                break;
            case 'y':
                rs.yRes = (int) getDouble(optarg);
                break;
            case 'P':
                rs.useCPUTime = true;
                rs.cores = getDouble(optarg, rs.cores);
                break;
            case '0':
                rs.bits = 10;
                break;
            case '2':
                rs.bits = 12;
                break;
            case 'n':
                rs.useTwoPass = false;
                break;
            case 'p':
                rs.useCPUTime = false;
                break;
            case 'k':
                rs.testFwdFrames = true;
                break;
            case 'K':
                rs.testAlternativeTunings = true;
                break;
            case ':':
                std::cout << "ERROR... option needs a value specified" << std::endl;
                return 1;
                break;
            case '?'://used for some unknown options
                std::cout << "Unknown option " << (char) optopt << ". For help, use scv -h" << std::endl;
                return 0;
            case 'h':
                printHelpMenu();
                return 0;
        }
    }
    if (rs.inputFile == "") {
        std::cout << "Please provide an input video file to test with -i file" << std::endl;
        return 0;
    }

    std::cout << "Input file: " << rs.inputFile << std::endl;
    if (rs.outputCSV)
        std::cout << "Output CSV file: " << rs.outputCSVFile << std::endl;
    if (rs.temporaryStorageLocation != "/tmp/scv/")
        std::cout << "Temporary folder: " << rs.temporaryStorageLocation << std::endl;
    if (rs.xRes > 0) {
        std::cout << "Reencode resolution: " << rs.xRes << "x" << rs.yRes << "p" << std::endl;
    } else {
        std::cout << "Reencode resolution: " << "[auto]" << "x" << rs.yRes << "p" << std::endl;
    }
    if (rs.bits != 8) {
        std::cout << "Color depth: " << rs.bits << std::endl;
    }
    if (rs.useQFactor) {
        std::cout << "VMAF target: " << rs.vmafTarget << std::endl;
        std::cout << "Because Q factor is used, resulting vmaf may not match this target" << std::endl;
    } else {
        std::cout << "VMAF target: " << rs.vmafTarget << " +- " << rs.vmafEpsilon << std::endl;
    }
    if (rs.targetTimeRatio) {
        if (rs.timeCostRatio > 0)
            std::cout << "Will spend " << rs.timeCostRatio << "x as long to reduce the end video size by half" << std::endl;
        else
            std::cout << "Using slowest possible cpu settings for encode" << std::endl;
    }
    else {
        if (rs.useCPUTime && rs.cores != 1.0)
            std::cout << "Will target encoding " << rs.timescaleTarget/rs.cores << "s of video per cpu second or " << rs.timescaleTarget << "s/real life second with " << rs.cores << " cores." << std::endl;
        else if (rs.useCPUTime)
            std::cout << "Will target encoding " << rs.timescaleTarget << "s of video per cpu second" << std::endl;
        else
            std::cout << "Will target encoding " << rs.timescaleTarget << "s of video per second." << std::endl << "Do not turn off your computer during this time" << std::endl;
    }
    if (!rs.useTwoPass) {
        std::cout << "WARNING: running with 1 pass video" << std::endl;
    }

    runner::doSimulations(rs);
    return 0;
}
