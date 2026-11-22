project('codec2-mod', 'c')

# Codec2's 3200 bps mode alone, with separate encoder and decoder states that
# the caller owns. Only its sources are used so far, by the Zephyr builds
# that list them in CMakeLists.txt.
codec2_mod_inc = include_directories('inc')
codec2_mod_src = files('src/analysis.c',
                       'src/codec2_mod.c',
                       'src/delta_lsp_cb.c',
                       'src/interp.c',
                       'src/kiss_fft.c',
                       'src/kiss_fftr.c',
                       'src/lpc.c',
                       'src/nlp.c',
                       'src/quantise.c',
                       'src/synthesis.c',
                       'src/util.c')
