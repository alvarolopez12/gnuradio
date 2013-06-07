/* -*- c++ -*- */
/*
 * Copyright 2013 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/filter/pfb_arb_resampler.h>
#include <gnuradio/logger.h>
#include <cstdio>
#include <stdexcept>

namespace gr {
  namespace filter {
    namespace kernel {
    
      pfb_arb_resampler_ccf::pfb_arb_resampler_ccf(float rate,
                                                   const std::vector<float> &taps,
                                                   unsigned int filter_size)
      {
        d_acc = 0; // start accumulator at 0

        /* The number of filters is specified by the user as the
           filter size; this is also the interpolation rate of the
           filter. We use it and the rate provided to determine the
           decimation rate. This acts as a rational resampler. The
           flt_rate is calculated as the residual between the integer
           decimation rate and the real decimation rate and will be
           used to determine to interpolation point of the resampling
           process.
        */
        d_int_rate = filter_size;
        set_rate(rate);

        d_last_filter = 0;
        
        d_filters = std::vector<fir_filter_ccf*>(d_int_rate);
        d_diff_filters = std::vector<fir_filter_ccf*>(d_int_rate);

        // Create an FIR filter for each channel and zero out the taps
        std::vector<float> vtaps(0, d_int_rate);
        for(unsigned int i = 0; i < d_int_rate; i++) {
          d_filters[i] = new fir_filter_ccf(1, vtaps);
          d_diff_filters[i] = new fir_filter_ccf(1, vtaps);
        }

        // Now, actually set the filters' taps
        set_taps(taps);
      }

      pfb_arb_resampler_ccf::~pfb_arb_resampler_ccf()
      {
        for(unsigned int i = 0; i < d_int_rate; i++) {
          delete d_filters[i];
          delete d_diff_filters[i];
        }
      }

      void
      pfb_arb_resampler_ccf::create_taps(const std::vector<float> &newtaps,
                                         std::vector< std::vector<float> > &ourtaps,
                                         std::vector<fir_filter_ccf*> &ourfilter)
      {
        unsigned int ntaps = newtaps.size();
        d_taps_per_filter = (unsigned int)ceil((double)ntaps/(double)d_int_rate);

        // Create d_numchan vectors to store each channel's taps
        ourtaps.resize(d_int_rate);

        // Make a vector of the taps plus fill it out with 0's to fill
        // each polyphase filter with exactly d_taps_per_filter
        std::vector<float> tmp_taps;
        tmp_taps = newtaps;
        while((float)(tmp_taps.size()) < d_int_rate*d_taps_per_filter) {
          tmp_taps.push_back(0.0);
        }

        // Partition the filter
        for(unsigned int i = 0; i < d_int_rate; i++) {
          // Each channel uses all d_taps_per_filter with 0's if not enough taps to fill out
          ourtaps[d_int_rate-1-i] = std::vector<float>(d_taps_per_filter, 0);
          for(unsigned int j = 0; j < d_taps_per_filter; j++) {
            ourtaps[d_int_rate - 1 - i][j] = tmp_taps[i + j*d_int_rate];
          }

          // Build a filter for each channel and add it's taps to it
          ourfilter[i]->set_taps(ourtaps[d_int_rate-1-i]);
        }
      }

      void
      pfb_arb_resampler_ccf::create_diff_taps(const std::vector<float> &newtaps,
                                                   std::vector<float> &difftaps)
      {
        // Calculate the differential taps (derivative filter) by
        // taking the difference between two taps. Duplicate the last
        // one to make both filters the same length.
        float tap;
        difftaps.clear();
        for(unsigned int i = 0; i < newtaps.size()-1; i++) {
          tap = newtaps[i+1] - newtaps[i];
          difftaps.push_back(tap);
        }
        difftaps.push_back(tap);
      }

      void
      pfb_arb_resampler_ccf::set_taps(const std::vector<float> &taps)
      {
        std::vector<float> dtaps;
        create_diff_taps(taps, dtaps);
        create_taps(taps, d_taps, d_filters);
        create_taps(dtaps, d_dtaps, d_diff_filters);
      }
 
      std::vector<std::vector<float> >
      pfb_arb_resampler_ccf::taps() const
      {
        return d_taps;
      }

      void
      pfb_arb_resampler_ccf::print_taps()
      {
        unsigned int i, j;
        for(i = 0; i < d_int_rate; i++) {
          printf("filter[%d]: [", i);
          for(j = 0; j < d_taps_per_filter; j++) {
            printf(" %.4e", d_taps[i][j]);
          }
          printf("]\n");
        }
      }

      void
      pfb_arb_resampler_ccf::set_rate(float rate)
      {
        d_dec_rate = (unsigned int)floor(d_int_rate/rate);
        d_flt_rate = (d_int_rate/rate) - d_dec_rate;
      }

      void
      pfb_arb_resampler_ccf::set_phase(float ph)
      {
        if((ph < 0) || (ph >= 2.0*M_PI)) {
          throw std::runtime_error("pfb_arb_resampler_ccf: set_phase value out of bounds [0, 2pi).\n");
        }
        
        float ph_diff = 2.0*M_PI / (float)d_filters.size();
        d_last_filter = static_cast<int>(ph / ph_diff);
      }

      float
      pfb_arb_resampler_ccf::phase() const
      {
        float ph_diff = 2.0*M_PI / static_cast<float>(d_filters.size());
        return d_last_filter * ph_diff;
      }

      int
      pfb_arb_resampler_ccf::filter(gr_complex *output,
                                    gr_complex *input,
                                    int nitems)
      {
        int i = 0, count = 0;//d_start_index;
        unsigned int j;
        gr_complex o0, o1;

        j = d_last_filter;

        while(i < nitems) {
          // start j by wrapping around mod the number of channels
          while(j < d_int_rate) {
            // Take the current filter and derivative filter output
            o0 = d_filters[j]->filter(&input[count]);
            o1 = d_diff_filters[j]->filter(&input[count]);

            output[i] = o0 + o1*d_acc;     // linearly interpolate between samples
            i++;

            // Adjust accumulator and index into filterbank
            d_acc += d_flt_rate;
            j += d_dec_rate + (int)floor(d_acc);
            d_acc = fmodf(d_acc, 1.0);
          }
          count += (int)(j / d_int_rate);
          j = j % d_int_rate;
        }
        d_last_filter = j;
      }

    } /* namespace kernel */
  } /* namespace filter */
} /* namespace gr */