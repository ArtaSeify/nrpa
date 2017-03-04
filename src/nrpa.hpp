// nrpa.hpp
// Made by Benjamin Negrevergne 
// Started on <2017-02-22 Wed>

#ifndef NRPA_HPP
#define NRPA_HPP

#include <unordered_map>
#include <cmath>
#include <limits>
#include "rollout.hpp"
#include "movemap.hpp"

/* Old constants kepts for compatibility with old game file. Their
 * values are set to old defaults, they have no effect on the nrpa
 * algorithm, but they might have an impact on the old game code. 
 */
extern const int MaxLevel; 
extern double k;
extern float constante;
extern float kAMAF;
extern double minNorm, maxNorm;
extern int SizeBeam [];
extern int levelPrint; 
extern int startLearning;



class Policy{

public:

  inline double prob(int code) const {
    auto hit = _probs.find(code);
    //    assert(hit != _probs.end()); 
    if( hit != _probs.end() )
      return hit->second;
    else
      return 0.;
  }

  inline void setProb(int code, double prob){
    _probs[code] = prob; 
  }

  inline double updateProb(int code, double delta){
    auto hit = _probs.find(code);
    if(hit != _probs.end()){
      hit->second += delta;
      return hit->second;
    }
    else{
      _probs[code] = delta;
      return delta; 
    }
  }

  inline void print(std::ostream &os) const{
    os<<"Policy: "<<std::endl; 
    for(auto it = _probs.begin(); it != _probs.end(); ++it){
      os<<"\tCode : "<<it->first<<" prob: "<<it->second<<std::endl;
    }
    os<<"End of Policy"<<std::endl; 
  }

  template <typename X, typename Y, typename Z>
  inline void print(std::ostream &os, const MoveMap<X,Y,Z> &movemap) const{
    os<<"Policy: "<<std::endl; 
    for(auto it = _probs.begin(); it != _probs.end(); ++it){
      os<<"\tCode : "<<movemap.move(it->first).hash<<" prob: "<<it->second<<std::endl;
    }
    os<<"End of Policy"<<std::endl; 
  }

private: 
  std::unordered_map<int, double> _probs; 
};

template <typename B, typename M,
	  typename H = std::hash<M>,
	  typename EQ = std::equal_to<M>>
class Nrpa{

public:

  static const int N = 10; 
  static constexpr double ALPHA = 1.0; 

  int _level; 
  double _bestScore; 
  Policy _policy; 
  Rollout _bestRollout; 
  
  vector<M> _bestRolloutMoves;

  vector<MoveMap<M, H, EQ>> _movemaps; //_movemaps[i] maps valid moves to code moves. There is one mapping for each step i in the rollout.

  vector<vector<int>> _legalMoveCodes; // codes of every legal moves at step i
  B _bestBoard; 



public:

  Nrpa(int level);
  double run(); 
  double playout ();
  void updatePolicy(const Rollout &rollout); 
  inline double bestScore(){ return _bestScore; }


  inline void printPolicy(std::ostream &os) const{
    _policy.print(os); 
  }
  
}; 

template <typename B,typename  M,typename  H,typename EQ>
Nrpa<B,M,H,EQ>::Nrpa(int level): _level(level),
				 _bestScore(std::numeric_limits<double>::lowest()){
}

template <typename B,typename  M,typename  H,typename EQ>
double Nrpa<B,M,H,EQ>::run(){
  using namespace std; 

  if (_level == 0) {
    return playout(); 
  }
  else {

    int last = 0;

    for (int i = 0; i < N; i++) {
      Nrpa sub(_level - 1); 
      sub._policy = _policy; //TODO Pass as an argument to run
      sub.run();

      if (sub.bestScore() >= _bestScore) {
	last = i;
	_bestScore = sub.bestScore(); 
	_bestRollout = sub._bestRollout;
	_legalMoveCodes = sub._legalMoveCodes; 
	// TODO this copy is unecessary if we can update with the rollout of sub. There is not need to store the rollout in any nrpa object except the one of level 0. 
	
	if (_level > 2) {
	  for (int t = 0; t < _level - 1; t++)
	    fprintf (stderr, "\t");
	  fprintf(stderr,"Level : %d, N:%d, score : %f\n", _level, i, _bestRollout.score());
	}

      }

      /* Update policy only a new best sequence is found. */ 
      updatePolicy(_bestRollout); 

      // TODO : fix 
      // if (stopOnTime && (indexTimeNRPA > nbTimesNRPA))
      //   return bestRollout.score();
    }
    return _bestScore;
  }
}

template <typename B,typename  M,typename  H,typename EQ>
void Nrpa<B,M,H,EQ>::updatePolicy(const Rollout &rollout){

  assert(rollout.length() <= _legalMoveCodes.size()); 

  Policy newPol = _policy; //TODO remove this useless copy!!

  for(int step = 0; step < rollout.length(); step++){
    int move = rollout.move(step); 
    newPol.updateProb(move, ALPHA); 

    double z = 0.; 
    const std::vector<int> &legalMoves = _legalMoveCodes[step]; 
    for(auto move = legalMoves.begin(); move != legalMoves.end(); ++move)
      z += exp (_policy.prob( *move ));
  
    for(auto move = legalMoves.begin(); move != legalMoves.end(); ++move){
      newPol.updateProb( *move, - ALPHA * (exp (_policy.prob( *move )) / z ));
    }
  }

  _policy = newPol; 
}

  
/* This code contains number of copies which are required to work with
 * standard Board class, so beware before optimizing.  */ 

template <typename B,typename  M,typename  H,typename EQ>
double Nrpa<B, M, H, EQ>::playout () {

  using namespace std; 
  assert(_bestRollout.length() == 0); 

  B board; 

  _bestRollout.moves()->reserve(board.maxLegalMoves()); // reserve space to store rollout moves
   
  while (true) {
    int step = board.length; 

    if (board.terminal ()) {

      double score = board.score(); 

      _bestRollout.setScore(score);
      _bestRollout.moves()->resize(board.length); // free up remaining space
      _bestScore = board.score(); 


      // if( score > _bestScoreNRPA ) {
      // 	_bestScoreNRPA = score; 
      // 	_bestBoard = board;
      // 	board.print (stderr);
      // }
	
      return score; 
    }

    /* board is at a non terminal step ... */


    /* Get all legal moves for this step */ 
    vector<M> legalMoves(board.maxLegalMoves()); 
    int nbMoves = board.legalMoves(&legalMoves.front());
    legalMoves.resize(nbMoves); 

    /* store legal move codes */
    _legalMoveCodes.resize(step+1, vector<int>(nbMoves)); 
    for(int i = 0; i < legalMoves.size(); i++){
            static H hasher; 
	    _legalMoveCodes[step][i] = hasher(legalMoves[i]);
	    // #ifdef CHECK_COLLISION
	    // 	    /* If each move has a unique code, this will never happen,
	    // 	       but for games with a very large number of moves this
	    // 	       may happen, in doubt, define CHECK_COLLISION to find
	    // 	       out. */
	    // 	    sort( _legalMoveCodes[i].begin(), _legalMoveCodes[i].end());
	    // 	    assert(adjacent_find(_legalMoveCodes[i].begin(),
	    // 				 _legalMoveCodes[i].end()) == _legalMoveCodes[i].end());
	    // #endif 
    }

    /* Compute probs for each move (code) */ 
    vector<double> moveProbs(nbMoves); 
    double sum = 0; 
    for (int i = 0; i < nbMoves; i++) {
      double prob = exp(_policy.prob( _legalMoveCodes[step][i] )); // TODO save exp computation
      moveProbs[i] = prob;
      sum += prob; 
    }

    /* Pick a move randomly according to the policy distribution */
    double r = (rand () / (RAND_MAX + 1.0)) * sum;
    int j = 0;
    double s = moveProbs[0];
    while (s < r) { 
      j++;
      s += moveProbs[j];
    }

    M newMove = legalMoves[j]; // this copy is required because move.play is non const
    int newMoveCode = _legalMoveCodes[step][j];

    _bestRollout.addMove(newMoveCode); 
    board.play(newMove); 
  }
  return 0.0;  
}


#endif 
