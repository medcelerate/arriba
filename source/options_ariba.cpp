#include <iostream>
#include <unordered_map>
#include <string>
#include <sstream>
#include <unistd.h>
#include <algorithm>
#include "common.hpp"
#include "options.hpp"
#include "options_ariba.hpp"

using namespace std;

options_t get_default_options() {
	options_t options;

	options.interesting_contigs = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 X Y";
	for (auto i = FILTERS.begin(); i != FILTERS.end(); ++i)
		options.filters[i->first] = true;
	options.evalue_cutoff = 0.4;
	options.min_support = 2;
	options.max_mismapper_fraction = 0.5;
	options.min_anchor_length = 20;
	options.homopolymer_length = 6;
	options.min_read_through_distance = 10000;
	options.print_supporting_reads = false;
	options.print_supporting_reads_for_discarded_fusions = false;
	options.low_tumor_content = false;
	options.max_kmer_content = 0.6;

	return options;
}

void print_usage(const string& error_message) {
	if (error_message != "")
		cerr << "ERROR: " << error_message << endl;

	options_t default_options = get_default_options();
	string valid_filters;
	for (auto i = default_options.filters.begin(); i != default_options.filters.end(); ++i) {
		if (i != default_options.filters.begin())
			valid_filters += ", ";
		valid_filters += i->first;
	}

	cerr << endl
	     << "Ariba RNA fusion detector" << endl
	     << "--------------------------" << endl
	     << "Version: " << ARIBA_VERSION << endl << endl
	     << "Ariba is a fast fusion detection algorithm. If finds RNA fusions " << endl
	     << "from the chimeric BAM file generated by the STAR RNA-Seq aligner." << endl << endl
	     << "Usage: ariba -c chimeric.bam [-r read_through.bam] -x rna.bam -g genes.bed -e exons.bed -o fusions.out " << endl
	     << "             [-a assembly.fa] [-b blacklists.tsv] [-k known_fusions.tsv]" << endl
	     << "             [OPTIONS]" << endl << endl
	     << wrap_help("-c FILE", "BAM file with chimeric alignments as generated by STAR. "
	                  "The file must be in BAM format, but not necessarily sorted.")
	     << wrap_help("-r FILE", "BAM file with read-through alignments as generated by "
	                  "'extract_read-through_fusions'. STAR does not report read-through "
	                  "fusions in the chimeric.bam file. Such fusions must be extracted "
	                  "manually from the rna.bam file. This is accomplished with the help "
	                  "of the utility 'extract_read-through_fusions'. For optimal "
	                  "performance, this should be done while STAR is running. Example:\n"
                          "STAR --outStd BAM [...] | tee rna.bam | \\\n"
	                  "extract_read-through_fusions -g genes.bed > read_through.bam")
	     << wrap_help("-x FILE", "BAM file with RNA-Seq data. The file must be sorted by "
	                  "coordinate and an index with the file extension .bai must be "
	                  "present. The file is used to estimate the mate gap distribution "
	                  "and to filter fusions with no expression around the "
	                  "breakpoints, which are likely false positives.")
	     << wrap_help("-g FILE", "BED file with gene annotation. The following columns are "
	                  "required: (1) contig, (2) gene_start, (3) gene_end, "
	                  "(4) gene_name, (5) ignored, (6) strand. The file may be gzip-compressed.")
	     << wrap_help("-e FILE", "BED file with exon annotation. The same columns are required "
	                  "as for the gene annotation (see -g). There should not be any "
	                  "exons outside genes. The file may be gzip-compressed.")
	     << wrap_help("-o FILE", "Output file with fusions that have passed all filters. The "
	                  "file contains the following columns separated by tabs:\n"
	                  "gene1: name of the gene that makes the 5' end\n"
	                  "gene2: name of the gene that makes the 3' end\n"
	                  "strand1: strand of gene1 as per annotation (see -g)\n"
	                  "strand2: strand of gene2 as per annotation (see -g)\n"
	                  "breakpoint1: coordinate of breakpoint in gene1\n"
	                  "breakpoint2: coordinate of breakpoint in gene2\n"
	                  "site1: site in gene1 (intergenic / exonic / intronic / splice-site)\n"
	                  "site2: site in gene2 (intergenic / exonic / intronic / splice-site)\n"
	                  "direction1: whether gene2 is fused to gene1 upstream (at a coordinate lower than breakpoint1) or downstream (at a coordinate higher than breakpoint1)\n"
	                  "direction2: whether gene1 is fused to gene2 upstream (at a coordinate lower than breakpoint2) or downstream (at a coordinate higher than breakpoint2)\n"
	                  "split_reads1: split read count in gene1\n"
	                  "split_reads2: split read count in gene2\n"
	                  "discordant_mates: discordant mate count\n"
	                  "e_value: 'expected value' reflecting how many fusions with the given number of supporting reads are expected by pure chance (lower is better)\n"
	                  "filters: why the fusion was discarded, numbers in brackets indicate the number of reads removed by the respective filter\n"
	                  "fusion_transcript: if -a is given, the sequence of a transcript which spans the fusion breakpoints (may be empty, when the breakpoint is close to an exon boundary)\n"
	                  "read_identifiers: if -I is given, the names of supporting reads")
	     << wrap_help("-O FILE", "Output file with fusions that were discarded due to "
	                  "filtering. See parameter -o for a description of the format.") 
	     << wrap_help("-a FILE", "FastA file with genome sequence (assembly). A FastA index "
	                  "with the extension .fai must be present. Ariba re-aligns reads to "
	                  "identify chimeric segments which were erroneously mapped to "
	                  "a different gene by STAR. A segment is thought to be a "
	                  "mismapper, if it also maps somewhere within the donor gene "
	                  "albeit with lower mapping quality. The assembly file is used "
	                  "to extract the sequence of the donor gene. Moreover, the output "
	                  "file will contain the sequence of a transcript spanning the "
	                  "fusion breakpoints.")
	     << wrap_help("-k FILE", "File containing known/recurrent fusions. Some cancer "
	                  "entities are often characterized by fusions between the same pair of genes. "
	                  "In order to boost sensitivity, a list of known fusions can be supplied using this parameter. "
	                  "The list must contain two columns with the names of the fused genes, "
	                  "separated by tabs. The 'promiscuous_genes' filter will be "
	                  "disabled for these pairs of genes, such that fusions are detected even "
	                  "in the presence of a level of noise (provided that no other filter "
	                  "discards the fusion). A useful list of recurrent fusions by cancer entity can "
	                  "be obtained from CancerGeneCensus. The file may be gzip-compressed.")
	     << wrap_help("-b FILE", "File containing blacklisted ranges. The file has two tab-separated "
	                  "columns. Both columns contain a genomic coordinate of the "
	                  "format 'contig:position' or 'contig:start-end'. Alternatively, the second "
	                  "column can contain one of the following keywords: any, split_read_donor, "
	                  "split_read_acceptor, split_read_any, discordant_mates. The file may be "
	                  "gzip-compressed.")
	     << wrap_help("-i CONTIGS", "A comma-/space-separated list of interesting contigs. Fusions "
	                  "between genes on other contigs are ignored. Contigs can be specified with "
	                  "or without the prefix \"chr\".\nDefault: " + default_options.interesting_contigs)
	     << wrap_help("-f FILTERS", "A comma-/space-separated list of filters to disable. By default "
	                  "all filters are enabled. Valid values: " + valid_filters)
	     << wrap_help("-E MAX_E-VALUE", "Ariba estimates the number of fusions with a given "
	                  "number of supporting reads which one would expect to see by random chance. "
	                  "If the expected number of fusions (e-value) is higher than this threshold, "
	                  "the fusion is discarded by the 'promiscuous_genes' filter. Note: "
	                  "Increasing this threshold can dramatically increase the "
	                  "number of false positives and may increase the runtime "
	                  "of time-consuming steps, most notably the 'mismappers' "
	                  "and 'no_expression' filters. Fractional values are "
	                  "possible. Default: " + to_string(default_options.evalue_cutoff))
	     << wrap_help("-s MIN_SUPPORTING_READS", "The 'min_support' filter discards all fusions "
	                  "with fewer than this many supporting reads (split reads and discordant "
	                  "mates combined). Default: " + to_string(default_options.min_support))
	     << wrap_help("-l", "This switch increases sensitivity in samples with low tumor content "
	                  "or subclonal fusions. When sequencing depth is high, the 'promiscuous_genes' filter "
	                  "removes fusions with few supporting reads. This may lead to true fusions "
	                  "being missed in samples with low tumor content. When this switch is set, "
	                  "fusions with fewer supporting reads than would be expected from the given "
	                  "sequencing depth will not be discarded. Sensitivity can be improved further by "
	                  "increasing the value of the parameter -s. Default: " + string((default_options.low_tumor_content) ? "on" : "off"))
	     << wrap_help("-m MAX_MISMAPPERS", "When more than this fraction of supporting reads "
	                  "turns out to be mismappers, the 'mismapper' filter "
	                  "discards the fusion. Default: " + to_string(default_options.max_mismapper_fraction))
	     << wrap_help("-H HOMOPOLYMER_LENGTH", "The 'homopolymer' filter removes breakpoints "
	                  "adjacent to homopolymers of the given length or more. Default: " + to_string(default_options.homopolymer_length))
	     << wrap_help("-D READ_THROUGH_DISTANCE", "The executable 'extract_read-through_fusions' extracts "
	                  "chimeric alignments from the BAM file with RNA-Seq data which could "
	                  "potentially originate from read-through fusions (fusions of neighboring "
	                  "genes). Any pair of mates where one of the mates does not map to the "
	                  "same gene as the other mate is considered a potential read-through fusion. "
	                  "Most of these alignments map to the UTRs of a gene, however, and are "
	                  "therefore false positives. The 'read_through' filter removes mates "
	                  "that map less than the given distance away from the gene of the other "
	                  "mate, unless both mates map to annotated genes. Default: " + to_string(default_options.min_read_through_distance))
	     << wrap_help("-A MIN_ANCHOR_LENGTH", "Alignment artifacts are often characterized by "
	                  "split reads coming from only gene and no discordant mates. Moreover, the split reads only "
	                  "align to a short stretch in one of the genes (<=20bp). The 'short_anchor' "
	                  "filter removes these fusions. This parameter sets the threshold in bp for "
	                  "what the filter considers short. Default: " + to_string(default_options.min_anchor_length))
	     << wrap_help("-K MAX_KMER_CONTENT", "The 'low_entropy' filter removes reads with "
	                  "repetitive 3-mers. If the 3-mers make up more than the given fraction "
	                  "of the sequence, then the read is discarded. Default: " + to_string(default_options.max_kmer_content))
	     << wrap_help("-I", "When set, the column 'read_identifiers' is populated with "
	                  "identifiers of the reads which support the fusion. The identifiers "
	                  "are separated by commas. Default: " + string((default_options.print_supporting_reads) ? "on" : "off"))
	     << wrap_help("-h", "Print help and exit.")
	     << "Questions or problems may be sent to: " << HELP_CONTACT << endl;
	exit(1);
}

options_t parse_arguments(int argc, char **argv) {
	options_t options = get_default_options();
	string disabled_filters;
	istringstream disabled_filters_ss;

	// parse arguments
	opterr = 0;
	int c;
	while ((c = getopt(argc, argv, "c:r:x:g:e:o:O:a:k:b:i:f:E:s:lm:H:D:A:K:Ih")) != -1) {
		switch (c) {
			case 'c':
				options.chimeric_bam_file = optarg;
				if (access(options.chimeric_bam_file.c_str(), R_OK) != 0) {
					cerr << "ERROR: File '" << options.chimeric_bam_file << "' not found." << endl;
					exit(1);
				}
				break;
			case 'r':
				options.read_through_bam_file = optarg;
				if (access(options.read_through_bam_file.c_str(), R_OK) != 0) {
					cerr << "ERROR: File '" << options.read_through_bam_file << "' not found." << endl;
					exit(1);
				}
				break;
			case 'x':
				options.rna_bam_file = optarg;
				if (access(options.rna_bam_file.c_str(), R_OK) != 0) {
					cerr << "ERROR: File '" << options.rna_bam_file << "' not found." << endl;
					exit(1);
				}
				if (access((options.rna_bam_file + ".bai").c_str(), R_OK) != 0) {
					cerr << "ERROR: File '" << options.rna_bam_file << ".bai' not found." << endl;
					exit(1);
				}
				break;
			case 'g':
				options.gene_annotation_file = optarg;
				if (access(options.gene_annotation_file.c_str(), R_OK) != 0) {
					cerr << "ERROR: File '" << options.gene_annotation_file << "' not found." << endl;
					exit(1);
				}
				break;
			case 'e':
				options.exon_annotation_file = optarg;
				if (access(options.exon_annotation_file.c_str(), R_OK) != 0) {
					cerr << "ERROR: File '" << options.exon_annotation_file << "' not found." << endl;
					exit(1);
				}
				break;
			case 'o':
				options.output_file = optarg;
				break;
			case 'O':
				options.discarded_output_file = optarg;
				break;
			case 'a':
				options.assembly_file = optarg;
				if (access(options.assembly_file.c_str(), R_OK) != 0) {
					cerr << "ERROR: File '" << options.assembly_file << "' not found." << endl;
					exit(1);
				}
				if (access((options.assembly_file + ".fai").c_str(), R_OK) != 0) {
					cerr << "ERROR: File '" << options.assembly_file << ".fai' not found." << endl;
					exit(1);
				}
				break;
			case 'b':
				options.blacklist_file = optarg;
				if (access(options.blacklist_file.c_str(), R_OK) != 0) {
					cerr << "ERROR: File '" << options.blacklist_file << "' not found." << endl;
					exit(1);
				}
				break;
			case 'k':
				options.known_fusions_file = optarg;
				if (access(options.known_fusions_file.c_str(), R_OK) != 0) {
					cerr << "ERROR: File '" << options.known_fusions_file << "' not found." << endl;
					exit(1);
				}
				break;
			case 'i':
				options.interesting_contigs = optarg;
				replace(options.interesting_contigs.begin(), options.interesting_contigs.end(), ',', ' ');
				break;
			case 'f':
				disabled_filters = optarg;
				replace(disabled_filters.begin(), disabled_filters.end(), ',', ' ');
				disabled_filters_ss.str(disabled_filters);
				while (disabled_filters_ss) {
					string disabled_filter;
					disabled_filters_ss >> disabled_filter;
					if (!disabled_filter.empty() && options.filters.find(disabled_filter) == options.filters.end()) {
						print_usage("Invalid argument to option -f: " + disabled_filter);
					} else
						options.filters[disabled_filter] = false;
				}
				break;
			case 'E':
				options.evalue_cutoff = atof(optarg);
				break;
			case 's':
				options.min_support = atoi(optarg);
				break;
			case 'l':
				options.low_tumor_content = true;
				break;
			case 'm':
				options.max_mismapper_fraction = atof(optarg);
				if (options.max_mismapper_fraction < 0 || options.max_mismapper_fraction > 1)
					print_usage(string("Argument to -") + ((char) optopt) + " must be between 0 and 1.");
				break;
			case 'H':
				options.homopolymer_length = atoi(optarg);
				break;
			case 'D':
				options.min_read_through_distance = atoi(optarg);
				break;
			case 'A':
				options.min_anchor_length = atoi(optarg);
				break;
			case 'K':
				options.max_kmer_content = atof(optarg);
				break;
			case 'I':
				if (!options.print_supporting_reads)
					options.print_supporting_reads = true;
				else
					options.print_supporting_reads_for_discarded_fusions = true;
				break;
			case '?':
				switch (optopt) {
					case 'c': case 'r': case 'x': case 'g': case 'e': case 'o': case 'O': case 'a': case 'k': case 'b': case 'i': case 'f': case 'E': case 's': case 'm': case 'H': case 'D': case 'A': case 'K':
						print_usage(string("Option -") + ((char) optopt) + " requires an argument.");
						break;
					default:
						print_usage(string("Unknown option: -") + ((char) optopt));
						break;
				}
			case 'h':
			default:
				print_usage();
		}
	}

	// check for mandatory arguments
	if (options.chimeric_bam_file.empty())
		print_usage("Missing mandatory option: -c");
	if (options.read_through_bam_file.empty())
		cerr << "WARNING: missing option: -r, no read-through fusions will be detected" << endl;
	if (options.rna_bam_file.empty())
		print_usage("Missing mandatory option: -x");
	if (options.gene_annotation_file.empty())
		print_usage("Missing mandatory option: -g");
	if (options.exon_annotation_file.empty())
		print_usage("Missing mandatory option: -e");
	if (options.output_file.empty())
		print_usage("Missing mandatory option: -o");
	if (options.filters["mismappers"] && options.assembly_file.empty())
        	print_usage("Filter 'mismappers' enabled, but missing option: -a");
	if (options.filters["blacklist"] && options.blacklist_file.empty())
		print_usage("Filter 'blacklist' enabled, but missing option: -b");

	return options;
}

