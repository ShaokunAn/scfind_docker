#include "QueryScore.h"
#include "typedef.h"
#include "utils.h"
#include "EliasFano.h"
# include <pybind11/stl.h>



QueryScore::QueryScore()
{}


void QueryScore::estimateExpression(const py::dict& gene_results, const EliasFanoDB& db, const py::list& datasets)
{

  std::cout << "calculating tfidf for the reduced expression matrix... " << std::endl;


  // Store temporarily the strings so we can insert those in the map
  std::vector<std::string> tmp_strings;
  for (auto item : gene_results) {
      tmp_strings.push_back(item.first.cast<std::string>());
  }
//  std::vector<std::string> tmp_strings = keys.cast<std::vector<std::string>>();

  const auto tmpl_cont = std::vector<double>(tmp_strings.size(), 0);

  int total_cells_in_universe = db.getTotalCells(datasets);

  std::vector<std::string>& gene_names = tmp_strings;
  py::list py_gene_names = py::cast(gene_names);

  py::dict results = db.totalCells(py_gene_names, datasets);
  std::vector<int> gene_support;
  for (auto item : results) {
      gene_support.push_back(item.second.cast<int>());
  }

  for (size_t gene_row = 0; gene_row < tmp_strings.size(); ++gene_row)
  {
    const std::string& gene = tmp_strings[gene_row];
    float gene_idf = total_cells_in_universe / ((float)db.genes.at(gene).total_reads);
    // get the current score of the gene
    GeneScore g = {0, gene_row, 0, gene_support[gene_row]};
    double& gene_score = this->genes.insert(std::make_pair(gene, g)).first->second.tfidf;

    py::dict cts = gene_results.attr("get")(gene).cast<py::dict>();

    std::vector<std::string> ct_names;
    for (auto item : cts) {
        ct_names.push_back(item.first.cast<std::string>());
    }

    for(auto const& cell_type : ct_names)
    {

      py::object item_celltype = cts.attr("get")(cell_type);
      const py::list& expr_indices = item_celltype.cast<py::list>();

      const auto ctid_it = db.cell_types.find(cell_type);
      CellTypeID ct_id = ctid_it->second;
      std::vector<double> expr_values = decompressValues(db.getEntry(gene, cell_type).expr, db.quantization_bits);
      if (expr_values.size() != (unsigned int)expr_indices.size())
      {
        std::cerr << "Corrupted DB!" << std::endl;
      }
      int expr_index = 0;
      for (auto const& cell_id_obj : expr_indices)
      {
        int cell_id = cell_id_obj.cast<int>();
        // Generate cell identifier
        CellID cell(ct_id, cell_id);
        
        // insert cell if it does not exist
        auto ins_res = tfidf.insert(std::make_pair(cell, std::make_pair(tmpl_cont, 0)));
        // assign the decompressed expression vector to the data structure
        std::vector<double>& tfidf_vec = ins_res.first->second.first;
        auto& gene_support = ins_res.first->second.second;
        
        
        // increase gene_support
        gene_support++;
        
        // tfidf calculation ( the expression value , the total reads of that cell and the gene transcript abundance)
        tfidf_vec[gene_row] = (expr_values[expr_index++] / db.cells.at(cell).reads) * gene_idf;
        gene_score += tfidf_vec[gene_row];
        
      }
    }
  }
}


unsigned int QueryScore::geneSetCutoffHeuristic(const float percentile)
{
  
  unsigned int min_genes = 7;
  bool estimate_cutoff = this->genes.size() > min_genes ? true : false;

  if (not estimate_cutoff)
  {
    std::cerr << "Cutoff set to 1 due because low number of genes (less than "<< min_genes << ")" <<std::endl;
    return 1;
  }
  
  std::vector<int> genes_subset(this->genes.size(), 0);
  
  // TODO : Explain a bit more what is happening here
  for (auto const& c : this->tfidf)
  {
    size_t i = 0;
    for (auto const& v : c.second.first)
    {
      genes_subset[i++] += v > 0 ? c.second.second - 1 : 0;
    }
  }
 
  for (auto& v : this->genes)
  {
    v.second.cartesian_product_sets = genes_subset[v.second.index];
    v.second.cartesian_product_sets /= this->genes.size();
//    const auto& current_gene_name = v.first;
    int union_sum = std::accumulate(
        this->genes.begin(),
        this->genes.end(),
        0,
        [](const int& sum, const std::pair<std::string,GeneScore>& gene_score) {
            return sum + gene_score.second.support_in_datasets;
        });

    float mean_overlap = float(union_sum) / tfidf.size(); // normalize by cell size
    // how much the genes contribute to the overlap
    mean_overlap /= log(this->genes.size());
    v.second.cartesian_product_sets *= mean_overlap;
  }

  for (auto const& v : this->genes)
  {  
    std::cerr << "Cutoff proposed for gene " << v.first << ": " << v.second.cartesian_product_sets <<" with support " << v.second.support_in_datasets << std::endl;
  }

  // Estimate cutoff using a heuristic
  std::vector<int> gene_proposed_cutoffs;
  for (auto const& v : this->genes)
  {
    gene_proposed_cutoffs.push_back(v.second.cartesian_product_sets);
  }
  std::sort(gene_proposed_cutoffs.begin(), gene_proposed_cutoffs.end());
  
  unsigned int cutoff = gene_proposed_cutoffs[int((gene_proposed_cutoffs.size() * percentile)+0.5)];

  std::cerr << "Cutoff for FP-growth estimated at the "<<percentile * 100 <<" of proposed cutoffs: " << cutoff << " cells" <<std::endl;
  return cutoff;

}



float QueryScore::cell_tfidf(const EliasFanoDB& db, const std::set<std::string>& gene_set)
{
 
  float score = 0;
  float min = genes[*(gene_set.begin())].tfidf;
  for(auto const& g : gene_set)
  {
    float tfidf = genes[g].tfidf;
    min = tfidf < min ? tfidf : min;
    score += tfidf;
  }
  score *= min;
  return score;
}



int QueryScore::calculate_cell_types(const std::set<std::string>&gene_set)
{
  return 0;
}