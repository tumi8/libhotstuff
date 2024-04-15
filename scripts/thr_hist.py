#!/usr/bin/env python3
'''
/**
 * Copyright 2018 VMware
 * Copyright 2018 Ted Yin
 * Copyright 2023 Chair of Network Architectures and Services, Technical University of Munich
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
'''
import argparse
import csv
import numpy as np
import os.path
import re
import sys

from datetime import datetime, timedelta

def remove_outliers(x, outlierConstant = 1.5):
    a = np.array(x)
    upper_quartile = np.percentile(a, 75)
    lower_quartile = np.percentile(a, 25)
    IQR = (upper_quartile - lower_quartile) * outlierConstant
    quartileSet = (lower_quartile - IQR, upper_quartile + IQR)
    resultList = []
    removedList = []
    for y in a.tolist():
        if y >= quartileSet[0] and y <= quartileSet[1]:
            resultList.append(y)
        else:
            removedList.append(y)
    return (resultList, removedList)

def str2datetime(s):
    parts = s.split('.')
    dt = datetime.strptime(parts[0], "%Y-%m-%d %H:%M:%S")
    return dt.replace(microsecond=int(parts[1]))

def plot_thr(fname):
    import matplotlib.pyplot as plt
    x = range(len(values))
    y = values
    plt.xlabel(r"time")
    plt.ylabel(r"tx/sec")
    plt.plot(x, y)
    plt.savefig(fname)
#    plt.show()

def plot_thr_2(fname):
    import matplotlib.pyplot as plt
    """
    fig, ax = plt.subplots()
    # values: [number of answered requests per second, for t in [0, duration]]
    #         ordered chronologically e.g. [4000,4002,3800,...]
    # lat_indexes: indeces of timestamps, that are at the timepoint for which
    #              the TPS ('values') value is calculated, in original order
    # lats: latencies of all answered request in original order
    x = values
    # so these points are actually not all latencies, but only the latencies
    # corresponding to "representative" timestamps (the one timestamp per second
    # where the TPS is evaluated and saved into "values")
    y = [lats[i] * 1e3 for i in lat_indexes]
    plt.xlabel(r"ops/sec")
    plt.ylabel(r"latency (ms)")
    plt.scatter(x, y)
    ax.set_ylim(bottom=0)
    plt.savefig(fname)
#    plt.show()
    """
    # values -> array of len ~60
    # lat_indexes -> array of len ~60, contains indeces into arr of
    #                len($number_of_received_replies)
    # lats -> array of len($number_received_replies)
    # goal:
    #   - plot all lats
    #   - lats which are between lat_indexes i and i+1, should be plotted as
    #     (x_(i), lat)

    x = []
    y = []

    measure_point_index = 0     # used to iterate through values/lat_indexes
    max_len_index = len(lat_indexes) - 2
    current_value = values[measure_point_index]
    current_lat_index = lat_indexes[measure_point_index]
    next_lat_index = lat_indexes[measure_point_index + 1]

    for i in range(0,len(lats)):
        if measure_point_index >= max_len_index:
            break

        if i >= next_lat_index:
            measure_point_index += 1
            current_lat_index = lat_indexes[measure_point_index]
            next_lat_index = lat_indexes[measure_point_index + 1]
            current_value = values[measure_point_index]

        lat = lats[i]
        x.append(current_value)
        y.append(lat * 1e3)

    fig, ax = plt.subplots()
    plt.xlabel(r"ops/sec")
    plt.ylabel(r"latency (ms)")
    plt.scatter(x, y)
    ax.set_ylim(bottom=0)
    plt.savefig(fname)
#    plt.show()

"""
    User added function to enable csv output of latency histogram values for
    TikZ
"""
def output_csv_thr_2(fname):
    # values: [number of answered requests per second, for t in [0, duration]]
    #         ordered chronologically e.g. [4000,4002,3800,...]
    # lat_indexes: indeces of timestamps, that are at the timepoint for which
    #              the TPS ('values') value is calculated, in original order
    # lats: latencies of all answered request in original order
#    x = values
    # so these points are actually not all latencies, but only the latencies
    # corresponding to "representative" timestamps (the one timestamp per second
    # where the TPS is evaluated and saved into "values")
#    y = [lats[i] * 1e3 for i in lat_indexes]

    # values -> array of len ~60
    # lat_indexes -> array of len ~60, contains indeces into arr of
    #                len($number_of_received_replies)
    # lats -> array of len($number_received_replies)
    # goal:
    #   - plot all lats
    #   - lats which are between lat_indexes i and i+1, should be plotted as
    #     (x_(i), lat)

    x = []
    y = []

    measure_point_index = 0     # used to iterate through values/lat_indexes
    max_len_index = len(lat_indexes) - 2
    current_value = values[measure_point_index]
    current_lat_index = lat_indexes[measure_point_index]
    next_lat_index = lat_indexes[measure_point_index + 1]

    for i in range(0,len(lats)):
        if measure_point_index >= max_len_index:
            break

        if i >= next_lat_index:
            measure_point_index += 1
            current_lat_index = lat_indexes[measure_point_index]
            next_lat_index = lat_indexes[measure_point_index + 1]
            current_value = values[measure_point_index]

        lat = lats[i]
        x.append(current_value)
        y.append(lat * 1e3)

    head_row = [ "OpS", "Lat" ]
    rows = [ row for row in zip(x,y) ]

    with open(fname, 'w', encoding='UTF-8') as file_handle:
        cwriter = csv.writer(file_handle)
        cwriter.writerow(head_row)
        for row in rows:
            cwriter.writerow(row)



if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--interval', type=float, default=1, required=False)
    parser.add_argument('--output', type=str, default="hist.png", required=False)
    parser.add_argument('--plot', action='store_true')
    parser.add_argument('--blocksize', type=int, default=1, required=False)
    args = parser.parse_args()
    commit_pat = re.compile('([^[].*) \[hotstuff info\] ([0-9.]*)$')
    interval = args.interval
    begin_time = None
    next_begin_time = None
    cnt = 0
    lats = []
    timestamps = []
    orig_timestamps = []
    values = []
    lat_indexes = []
    count = 0
    for line in sys.stdin:
        m = commit_pat.match(line)
        if m:
            timestamps.append(str2datetime(m.group(1)))
            lats.append(float(m.group(2)))

    # ignore all measurements from the last pipeline execution to prevent
    # influences from shutdown execution
    timestamps = timestamps[:len(timestamps)-args.blocksize*3]
    lats = lats[:len(lats)-args.blocksize*3]

    time = (timestamps[-1] - timestamps[0]).total_seconds()
    count = len(lats)
    print("entire count of ops is = {:.3f}".format(count))
    print("entire time is = {:.3f}".format(time))

    orig_timestamps = timestamps.copy()
    timestamps.sort()
    print("now timestamp evaluation")
    for timestamp in timestamps:
        if begin_time is None:
            begin_time = timestamp
            next_begin_time = timestamp + timedelta(seconds=interval)
        while timestamp >= next_begin_time:
            begin_time = next_begin_time
            next_begin_time += timedelta(seconds=interval)
            values.append(cnt)
            lat_indexes.append(orig_timestamps.index(timestamp))
            cnt = 0
        cnt += 1
    lat_indexes.append(orig_timestamps.index(timestamps[-1]))
    values.append(cnt)

    # write out latency histogram values as csv
    csvname = ''
    if args.output.find('.') > -1:
        csvname = args.output.split('.')[0] + ".csvreal"
    else:
        csvname = args.output + ".csvreal"
    output_csv_thr_2(csvname)

    if args.plot:
        plot_thr(args.output)
        plot_thr_2("second_" + args.output)
    print("lat = {:.3f}ms".format(sum(lats) / len(lats) * 1e3))
    print("ops/sec = {:.3f}".format(count/time))
    lats, _ = remove_outliers(lats)
    print("lat = {:.3f}ms".format(sum(lats) / len(lats) * 1e3))
