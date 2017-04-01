// nrpa.inl
// Made by Benjamin Negrevergne 
// Started on <2017-03-08 Wed>
#include <limits>
#include <algorithm>
#include <sstream> 
#include "threadpool.hpp"


template <typename B,typename  M, int L, int PL, int LM>
Nrpa<B,M,L,PL,LM>::Nrpa(int maxThreads, int parLevel, bool threadStats){
  assert(maxThreads < MAX_THREADS); 

  if(maxThreads == 1){
    _nbThreads = 1;
    _parLevel = 0; 
    /* Only one thread is nedded, we don't need to create the threadpool */ 
  }
  else{
    if( ! _threadPool.initialized() ){
      if(maxThreads == 0)
	_threadPool.init(maxThreads, threadStats);
      else
	_threadPool.init(maxThreads, threadStats);
      _nbThreads = _threadPool.nbThreads(); 
      _parLevel = parLevel; 
    }
  }
}

template <typename B,typename  M, int L, int PL, int LM>
double Nrpa<B,M,L,PL,LM>::run(int level, int nbIter, int timeout){
  assert(level < L); 
  assert(nbIter < MAX_ITER); 

  _startLevel = level; 
  _nbIter = nbIter; 
  _timeout = false; 
  _done = false; 

  setTimers(timeout, true); 

  Policy policy; 
  double score =  run(&_nrpa[level], level, policy);


  _doneMutex.lock();
  _done = true; 
  _doneCond.notify_all();
  _doneMutex.unlock(); 

  

  cout<<"Bestscore: "<<score<<endl;
  
  return score; 
}

template <typename B,typename  M, int L, int PL, int LM>
double Nrpa<B,M,L,PL,LM>::test(const Options &o){

  int nbRun = o.numRun;
  int nbIter = o.numIter;
  int timeout = o.timeout;
  int nbThreads = o.numThread;
  int level = o.numLevel;
  int parLevel = o.parallelLevel; 

  errorif(level >= L, "level should be lower than L template argument."); 
  
  double avgscore = 0;
  double maxscore = numeric_limits<double>::lowest(); 

  std::vector<NrpaStats[MAX_ITER]> iterStats(o.numRun);
  std::vector<NrpaStats[MAX_TIME_EVENTS]> timeStats(o.numRun);
  std::vector<int> nbTimeStats(o.numRun); 

  for(int i = 0; i < o.numRun; i++){
    Nrpa<B,M,L,PL,LM> nrpa(nbThreads, parLevel, o.threadStats); 
    double score = nrpa.run(level, nbIter, timeout);
    avgscore += score;
    maxscore = max(maxscore,  score); 

    if(o.stats){
      copy(nrpa._iterStats, nrpa._iterStats + nbIter, iterStats[i]);
      copy(nrpa._timerStats, nrpa._timerStats + nrpa._nbTimerEvents, timeStats[i]);
    }
  }
  
  if(o.stats){
    /* TODO: move into a separate function */
    fstream fs;
    ostringstream filename;
    filename<< "plots/dat/nrpa_stats";
    filename<<"_nbRun."<<nbRun;
    filename<<"_level."<<level;
    filename<<"_nbIter."<<nbIter;
    filename<<"_timeout."<<timeout;
    filename<<"_nbThreads."<<nbThreads;
    filename<<".dat";
    if(o.tag != "")
      filename<<"."<<o.tag;

    fs.open(filename.str(), fstream::out);
    fs<<"# nbRun "<<nbRun<<" level "<<level<<" nbIter "<<nbIter<<" timeout "<<timeout<<" nbThreads "<<nbThreads<<endl;
    for(int i = 0; i < nbRun; i++){
      for(int j = 0; j < nbIter; j++){
	NrpaStats &s = iterStats[i][j]; 
	fs<<j<<" "<<s.date<<" "<<s.bestScore<<" "<<"\n";
      }
    }
    fs.close(); 

    filename<<".timer"; 
    fs.open(filename.str(), fstream::out);
    fs<<"# nbRun "<<nbRun<<" level "<<level<<" nbIter "<<nbIter<<" timeout "<<timeout<<" nbThreads "<<nbThreads<<endl;
    for(int i = 0; i < nbRun; i++){
      for(int j = 0; j < MAX_TIME_EVENTS; j++){
	NrpaStats &s = timeStats[i][j]; 
	fs<<j<<" "<<s.date<<" "<<s.bestScore<<" "<<"\n";
      }
    }
    fs.close(); 

  }

  cout<<"Avgscore: "<< avgscore / nbRun<<endl; 
  cout<<"Bestscore-overall: "<< maxscore <<endl; 
  return avgscore / nbRun; 
}

template <typename B,typename  M, int L, int PL, int LM>
double Nrpa<B,M,L,PL,LM>::test(int nbRun, int level, int nbIter, int timeout, int nbThreads){
  Options o;
  o.numRun = nbRun;
  o.numLevel = level;
  o.numIter = nbIter;
  o.timeout = timeout;
  o.numThread = nbThreads;
  test(o); 
}

template <typename B,typename  M, int L, int PL, int LM>
double Nrpa<B,M,L,PL,LM>::run(NrpaLevel *nl, int level, const Policy &policy){
  using namespace std; 
  assert(level < L); 

  double score; 

  if (level == 0) {
    score = nl->playout(policy); 
  }
  else if(_nbThreads > 1 && level == _parLevel){
    score = runpar(nl, level, policy); 
  }
  else{
    score = runseq(nl, level, policy); 
  }

  return score; 

}

template <typename B,typename  M, int L, int PL, int LM>
double Nrpa<B,M,L,PL,LM>::runseq(NrpaLevel *nl, int level, const Policy &policy){
  using namespace std; 
  assert(level < L); 
  assert(level != 0); // level 0 should be a call to rollout 

  nl->bestRollout.reset(); 
  nl->levelPolicy = policy; 

  /* sequential call */ 
  NrpaLevel *sub; 
  if(level > _parLevel)
    sub = &_nrpa[level -1];
  else
    sub = new NrpaLevel;


  for(int i = 0; i < _nbIter; i++){
    double score = run(sub, level - 1, nl->levelPolicy); 
    if (score >= nl->bestRollout.score()) {
      int length = sub->bestRollout.length(); 
      nl->bestRollout = sub->bestRollout;
      nl->legalMoveCodes = sub->legalMoveCodes; 
	
      if (level > L - 3) {
	for (int t = 0; t < level - 1; t++)
	  fprintf (stdout, "\t");
	fprintf(stdout,"Level : %d, N:%d, score : %f\n", level, i, nl->bestRollout.score());
      }
    }

    if(level == _startLevel){
      recordIterStats(i, *nl); 
    }

    if(_done) { 
      if( level <= _parLevel) delete sub; 
      return nl->bestRollout.score(); 
    }

    if(i != _nbIter - 1)
      nl->updatePolicy(); 
  }
  
  /* Cleanup */ 
  if( level <= _parLevel) delete sub; 

  return nl->bestRollout.score();

}

template <typename B,typename  M, int L, int PL, int LM>
double Nrpa<B,M,L,PL,LM>::runpar(NrpaLevel *nl, int level, const Policy &policy){
  using namespace std; 
  assert(level < L); 
  assert(level != 0); // level 0 should be a call to rollout

  nl->bestRollout.reset(); 
  nl->levelPolicy = policy; 
    
  //  static ThreadPool t(maxThreads); 
  //  static const int nbThreads = t.nbThreads(); 
  //    NrpaLevel *subs = &_subs; 

  for(int i = 0; i < _nbIter; i+= _nbThreads){
    /* Run n thraeds */ 
    for(int j = 0; j < _nbThreads; j++){
      _subs[j].result = _threadPool.submit([ this, nl, level, j ]() -> int {
	  NrpaLevel *sub = &_subs[j];
	  run(sub, level - 1, nl->levelPolicy); return 1; });
    }


    /* fetch the best rollout among all parallel runs (if any better than before) */ 
    int best = -1; 
    double bestScore = nl->bestRollout.score();// numeric_limits<double>::lowest(); 
    for(int j = 0; j < _nbThreads; j++){
      _subs[j].result.wait();
      if(_subs[j].bestRollout.score() >= bestScore){
	bestScore = _subs[j].bestRollout.score(); 
	best = j;
      }
    }
    if(best >= 0){
      nl->bestRollout = _subs[best].bestRollout; // TODO is this copy necessary
      nl->legalMoveCodes = _subs[best].legalMoveCodes;
    }

    if(_done) { return nl->bestRollout.score(); }

    nl->updatePolicy( ALPHA * _nbThreads );
  }

  return nl->bestRollout.score();

}

template <typename B,typename  M, int L, int PL, int LM>
double Nrpa<B,M,L,PL,LM>::NrpaLevel::playout (const Policy &policy) {
  using namespace std; 
  
  B board; 

  bestRollout.reset(); 
  legalMoveCodes.setNbSteps(0); 

  while(! board.terminal ()) {

    /* board is at a non terminal step, make a new move ... */
    int step = board.length; 

    /* Get all legal moves for this step */ 
    M moves [LM];
    int nbMoves = board.legalMoves (moves);

    double moveProbs [LM];

    legalMoveCodes.setNbSteps(step + 1); 
    legalMoveCodes.setNbMoves(step, nbMoves); 
    for (int i = 0; i < nbMoves; i++) {
      int c = board.code (moves [i]);
      moveProbs [i] = exp (policy.prob(c));
      legalMoveCodes.setMove(step, i, c); 
    }

    double sum = moveProbs[0]; 
    for (int i = 1; i < nbMoves; i++) {
      sum += moveProbs[i]; 
    }


    /* Pick a move randomly according to the policy distribution */
    double r = (rand () / (RAND_MAX + 1.0)) * sum;
    int j = 0;
    double s = moveProbs[0];
    while (s < r) { 
      j++;
      s += moveProbs[j];
    }

    /* Store move, movecode, and actually play the move */
    assert(step == bestRollout.length()); 
    bestRollout.addMove(legalMoveCodes.move(step, j)); 
    board.play(moves[j]); 
  }

  /* Board is terminal */ 

  double score = board.score(); 
  bestRollout.setScore(score);
  return score; 
  
}


template <typename B,typename M, int L, int PL, int LM>
void Nrpa<B,M,L,PL,LM>::NrpaLevel::updatePolicy( double alpha ){

  //  assert(rollout.length() <= _nrpa[level].legalMoveCodes.size()); 
  using namespace std; 

  Policy newPol;
  int length = bestRollout.length(); 

  //  newPol = *levelPolicy; //complete copy is not necessary

  /* Copy data for the legal moves only into a new policy */ 
  for (int step = 0; step < length; step++) 
    for (int i = 0; i < legalMoveCodes.nbMoves(step);  i++){
      newPol.setProb (legalMoveCodes.move(step, i), levelPolicy.prob (legalMoveCodes.move(step,i)));
    }

  for(int step = 0; step < length; step++){
    int code = bestRollout.move(step); 
    newPol.setProb(code, newPol.prob(code) + alpha);

    double z = 0.; 
    for(int i = 0; i < legalMoveCodes.nbMoves(step); i++)
      z += exp (levelPolicy.prob( legalMoveCodes.move(step,i) ));

    for(int i = 0; i < legalMoveCodes.nbMoves(step); i++){
      int move = legalMoveCodes.move(step, i); 
      newPol.updateProb(move, - alpha * exp (levelPolicy.prob(move)) / z );
    }

  }

  /* Copy updated data back into the policy */ 
  for (int step = 0; step < length; step++) 
    for (int j = 0; j < legalMoveCodes.nbMoves(step); j++)
      levelPolicy.setProb (legalMoveCodes.move(step, j), newPol.prob(legalMoveCodes.move(step,j) ));

  //  *levelPolicy = newPol; 

}

template <typename B,typename M, int L, int PL, int LM>
Nrpa<B,M,L,PL,LM>::NrpaStats::NrpaStats(): bestScore(numeric_limits<double>::lowest()),
					   iter(0), 
					   date(0){}

template <typename B,typename M, int L, int PL, int LM>
void Nrpa<B,M,L,PL,LM>::NrpaStats::prettyPrint(){
  cout<<"score: "<<bestScore<<"\n";
  cout<<"date: "<<date<<"\n";
}

template <typename B,typename M, int L, int PL, int LM>
void Nrpa<B,M,L,PL,LM>::initStats(){
  fill_n(_iterStats, MAX_ITER, NrpaStats()); // reset stats
  fill_n(_timerStats, MAX_TIME_EVENTS, NrpaStats()); // reset stats
  _timerEvent = 0; 
  _startTime = clock(); 
  _nbTimerEvents = 0; 
}

template <typename B,typename M, int L, int PL, int LM>
void Nrpa<B,M,L,PL,LM>::recordIterStats(int iter, const NrpaLevel &nl){
  float date =  static_cast<float>(clock() - _startTime) / CLOCKS_PER_SEC; 
  _iterStats[iter].bestScore = nl.bestRollout.score(); 
  _iterStats[iter].date =  date; 
}

template <typename B,typename M, int L, int PL, int LM>
void Nrpa<B,M,L,PL,LM>::recordTimeStats(float date, int eventIdx,  const NrpaLevel &nl){
  assert( _nbTimerEvents < MAX_TIME_EVENTS); 
  _timerStats[_nbTimerEvents].date = date; 
  _timerStats[_nbTimerEvents].bestScore = nl.bestRollout.score(); 
  _timerStats[_nbTimerEvents].eventIdx = eventIdx;
  _nbTimerEvents++; 
}

template <typename B,typename M, int L, int PL, int LM>
void Nrpa<B,M,L,PL,LM>::setTimers(int timeout, int timeStats){
  using namespace chrono;

  int timerEvents[MAX_TIME_EVENTS];
  int i = 0; 
  if(timeStats){
    for(i = 0; i < MAX_TIME_EVENTS; i++){
      timerEvents[i] = 1<<i; 
      if(timeout > 0 && timerEvents[i] >= timeout){
	timerEvents[i] = timeout;
	break; 
      }
    }
    initStats(); 
  }
  else{ 
    timerEvents[i] = timeout; 
  }

  int _lastEventIdx = i; 

  auto now = system_clock::now();
  thread t([this, now, timerEvents, _lastEventIdx, timeStats] {
      unique_lock<mutex> lk(_doneMutex);
      for(int i = 0; !_done && i <= _lastEventIdx; i++){
  	_doneCond.wait_until(lk, now + seconds(timerEvents[i]));
	float d =  duration_cast<seconds>(system_clock::now() - now).count(); 
	cout<<"TIMER EVENT at "<<d<<" last idx "<<_lastEventIdx<<" i "<<i<<endl;
	if(timeStats){
	  recordTimeStats(d, i, _nrpa[_startLevel]);
	}
	if(i == _lastEventIdx) {
	  _done = true; 
	  cout<<"Timeout! "<<endl; 
	}
      }

    });
  t.detach(); 
}

template <typename B,typename M, int L, int PL, int LM>
void Nrpa<B,M,L,PL,LM>::errorif(bool cond, const std::string &msg){
  if(cond){
    cerr<<"Error : "<<msg<<endl;
    exit(1);
  }
}



/* Instanciation of static NRPA structures */ 
template <typename B,typename M, int L, int PL, int LM>
typename Nrpa<B,M,L,PL,LM>::NrpaLevel Nrpa<B,M,L,PL,LM>::_nrpa[L]; 

template <typename B, typename M, int L, int PL, int LM>
int Nrpa<B,M,L,PL,LM>::_nbThreads; 

template <typename B, typename M, int L, int PL, int LM>
int Nrpa<B,M,L,PL,LM>::_parLevel; 

template <typename B, typename M, int L, int PL, int LM>
ThreadPool Nrpa<B,M,L,PL,LM>::_threadPool; 

template <typename B, typename M, int L, int PL, int LM>
typename Nrpa<B,M,L,PL,LM>::NrpaLevel Nrpa<B,M,L,PL,LM>::_subs[MAX_THREADS]; 

template <typename B, typename M, int L, int PL, int LM>
typename Nrpa<B,M,L,PL,LM>::NrpaStats Nrpa<B,M,L,PL,LM>::_iterStats[MAX_ITER]; 

template <typename B, typename M, int L, int PL, int LM>
typename Nrpa<B,M,L,PL,LM>::NrpaStats Nrpa<B,M,L,PL,LM>::_timerStats[MAX_TIME_EVENTS]; 



