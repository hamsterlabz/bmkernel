-- Data.FiniteMap compat shim: the pre-containers finite-map API the 2004
-- shootout sources import, expressed over Data.Map.  Only the functions
-- the suite uses.
module Data.FiniteMap (FiniteMap, listToFM, lookupWithDefaultFM) where
import qualified Data.Map as M
type FiniteMap k v = M.Map k v
listToFM :: Ord k => [(k, v)] -> FiniteMap k v
listToFM = M.fromList
lookupWithDefaultFM :: Ord k => FiniteMap k v -> v -> k -> v
lookupWithDefaultFM m d k = M.findWithDefault d k m
