#!/bin/bash
# Copyright 2017  David Snyder
#           2017  Matthew Maciejewski
# Apache 2.0.
#
# This is still a work in progress, but implements something similar to
# Greg Sell's and Daniel Garcia-Romero's iVector-based diarization system
# in 'Speaker Diarization With PLDA I-Vector Scoring And Unsupervised
# Calibration'.  The main difference is that we haven't implemented the
# VB resegmentation yet.

. ./cmd.sh
. ./path.sh
set -e
mfccdir=`pwd`/mfcc
vaddir=`pwd`/mfcc
data_root=/export/corpora5/LDC
num_components=2048
ivector_dim=128

# Prepare a collection of NIST SRE data. This will be used to train the UBM,
# iVector extractor and PLDA model.
local/make_sre.sh $data_root data

# Prepare SWB for UBM and iVector extractor training.
local/make_swbd2_phase2.pl /export/corpora5/LDC/LDC99S79 \
                           data/swbd2_phase2_train
local/make_swbd2_phase3.pl /export/corpora5/LDC/LDC2002S06 \
                           data/swbd2_phase3_train
local/make_swbd_cellular1.pl /export/corpora5/LDC/LDC2001S13 \
                             data/swbd_cellular1_train
local/make_swbd_cellular2.pl /export/corpora5/LDC/LDC2004S07 \
                             data/swbd_cellular2_train

# Prepare the Callhome portion of NIST SRE 2000.
local/make_callhome.sh /export/corpora/NIST/LDC2001S97/ data/

utils/combine_data.sh data/train \
  data/swbd_cellular1_train data/swbd_cellular2_train \
  data/swbd2_phase2_train data/swbd2_phase3_train data/sre

# The script local/make_callhome.sh splits callhome into two parts, called
# callhome1 and callhome2.  Each partition is treated like a held-out
# dataset, and used to estimate various quantities needed to perform
# diarization on the other part (and vice versa).
for name in sre train callhome1 callhome2; do
  steps/make_mfcc.sh --mfcc-config conf/mfcc.conf --nj 40 --cmd "$train_cmd" \
    --write-utt2num-frames true data/$name exp/make_mfcc $mfccdir
  utils/fix_data_dir.sh data/$name
done

for name in sre train callhome1 callhome2; do
  sid/compute_vad_decision.sh --nj 40 --cmd "$train_cmd" \
    data/$name exp/make_vad $vaddir
  utils/fix_data_dir.sh data/$name
done

# Create segments for ivector extraction for PLDA training data.
echo "0.01" > data/sre/frame_shift
diarization/vad_to_segments.sh --nj 40 --cmd "$train_cmd" \
  data/sre data/sre_segmented

# Reduce the amount of training data for the PLDA,
utils/subset_data_dir.sh data/sre_segmented 128000 data/sre_segmented_128k

# Reduce the amount of training data for the UBM.
utils/subset_data_dir.sh data/train 16000 data/train_16k
utils/subset_data_dir.sh data/train 32000 data/train_32k

# Train UBM and i-vector extractor.
diarization/train_diag_ubm.sh --cmd "$train_cmd -l mem_free=20G,ram_free=20G" \
  --nj 40 --num-threads 8 --delta-order 1 data/train_16k $num_components \
  exp/diag_ubm_$num_components

diarization/train_full_ubm.sh --nj 40 --remove-low-count-gaussians false \
  --cmd "$train_cmd -l mem_free=25G,ram_free=25G" data/train_32k \
  exp/diag_ubm_$num_components exp/full_ubm_$num_components

diarization/train_ivector_extractor.sh \
  --cmd "$train_cmd -l mem_free=35G,ram_free=35G" \
  --ivector-dim $ivector_dim --num-iters 5 \
  exp/full_ubm_$num_components/final.ubm data/train \
  exp/extractor_c${num_components}_i${ivector_dim}

# Extract iVectors for the SRE, which is our PLDA training
# data.  A long period is used here so that we don't compute too
# many iVectors for each recording.
diarization/extract_ivectors.sh --cmd "$train_cmd --mem 25G" \
  --nj 40 --window 3.0 --period 10.0 --min-segment 1.5 \
  --hard-min true exp/extractor_c${num_components}_i${ivector_dim} \
  data/sre_segmented_128k exp/ivectors_sre_segmented_128k

# Extract iVectors for the two partitions of callhome.
diarization/extract_ivectors.sh --cmd "$train_cmd --mem 20G" \
  --nj 40 --window 1.5 --period 0.75 \
  --min-segment 0.5 exp/extractor_c${num_components}_i${ivector_dim} \
  data/callhome1 exp/ivectors_callhome1

diarization/extract_ivectors.sh --cmd "$train_cmd --mem 20G" \
  --nj 40 --window 1.5 --period 0.75 \
  --min-segment 0.5 exp/extractor_c${num_components}_i${ivector_dim} \
  data/callhome2 exp/ivectors_callhome2

# Train a PLDA model on SRE, using callhome1 to whiten.
# We will later use this to score iVectors in callhome2.
run.pl exp/ivectors_callhome1/log/plda.log \
  ivector-compute-plda ark:exp/ivectors_sre_segmented_128k/spk2utt \
  "ark:ivector-subtract-global-mean scp:exp/ivectors_sre_segmented_128k/ivector.scp ark:- | transform-vec exp/ivectors_callhome1/transform.mat ark:- ark:- | ivector-normalize-length ark:- ark:- |" \
  exp/ivectors_callhome1/plda || exit 1;

# Train a PLDA model on SRE, using callhome2 to whiten.
# We will later use this to score iVectors in callhome1.
run.pl exp/ivectors_callhome2/log/plda.log \
  ivector-compute-plda ark:exp/ivectors_sre_segmented_128k/spk2utt \
  "ark:ivector-subtract-global-mean scp:exp/ivectors_sre_segmented_128k/ivector.scp ark:- | transform-vec exp/ivectors_callhome2/transform.mat ark:- ark:- | ivector-normalize-length ark:- ark:- |" \
  exp/ivectors_callhome2/plda || exit 1;

# Perform PLDA scoring on all pairs of segments for each recording.
# The first directory contains the PLDA model that used callhome2
# to perform whitening (recall that we're treating callhome2 as a
# heldout dataset).  The second directory contains the iVectors
# for callhome1.
diarization/score_plda.sh --cmd "$train_cmd --mem 4G" \
  --nj 20 exp/ivectors_callhome2 exp/ivectors_callhome1 \
  exp/ivectors_callhome1/plda_scores

# Do the same thing for callhome2.
diarization/score_plda.sh --cmd "$train_cmd --mem 4G" \
  --nj 20 exp/ivectors_callhome1 exp/ivectors_callhome2 \
  exp/ivectors_callhome2/plda_scores

# Cluster the PLDA scores using agglomerative hierarchical clustering,
# using a hand-tuned threshold.
diarization/cluster.sh --cmd "$train_cmd --mem 4G" --nj 20 --threshold 0.522 \
  exp/ivectors_callhome1/plda_scores exp/ivectors_callhome1/plda_scores

diarization/cluster.sh --cmd "$train_cmd --mem 4G" --nj 20 --threshold 0.488 \
  exp/ivectors_callhome2/plda_scores exp/ivectors_callhome2/plda_scores

# Result using using unsupervised calibration
# OVERALL SPEAKER DIARIZATION ERROR = 10.32 percent of scored speaker time  `(ALL)
export PATH=$PATH:$KALDI_ROOT/tools/sctk/bin
mkdir -p results
cat exp/ivectors_callhome1/plda_scores/rttm exp/ivectors_callhome2/plda_scores/rttm \
  | md-eval.pl -1 -c 0.25 -r data/callhome/fullref.rttm -s - 2> results/threshold.log \
  | tee results/DER_threshold.txt

# Now try clustering using the oracle number of speakers.
diarization/cluster.sh --cmd "$train_cmd --mem 4G" \
  --spk2num data/callhome/reco2num \
  exp/ivectors_callhome1/plda_scores exp/ivectors_callhome1/plda_scores_num_spk

diarization/cluster.sh --cmd "$train_cmd --mem 4G" \
  --spk2num data/callhome/reco2num \
  exp/ivectors_callhome2/plda_scores exp/ivectors_callhome2/plda_scores_num_spk

# Result if the number of speakers is known in advance
# OVERALL SPEAKER DIARIZATION ERROR = 9.26 percent of scored speaker time  `(ALL)
cat exp/ivectors_callhome1/plda_scores_num_spk/rttm \
  exp/ivectors_callhome2/plda_scores_num_spk/rttm \
  | md-eval.pl -1 -c 0.25 -r data/callhome/fullref.rttm -s - 2> results/num_spk.log \
  | tee results/DER_num_spk.txt