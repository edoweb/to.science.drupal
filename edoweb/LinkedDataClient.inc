<?php
/**
 * Copyright 2013 hbz NRW (http://www.hbz-nrw.de/)
 *
 * This file is part of regal-drupal.
 *
 * regal-drupal is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * regal-drupal is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with regal-drupal.  If not, see <http://www.gnu.org/licenses/>.
 */

class LinkedDataClient {

  private static $__triple_store = null;

  /*
   * Load an entity from the API.
   *
   * @param $entity
   *   The drupal entity to load data into
   */
  public function load($remote_id, $rdf_model = null) {
    if (is_null($rdf_model) && (! $rdf_model = $this->getRDF(_edoweb_expand_curie($remote_id)))) {
      return false;
    } else {
      // FIXME: workaround for API bug after HTTP PATCHing access rights
      // for a subtree (i.e. resource/all).
      // FIXME: the following is a resource hog
      // if ("" === "$rdf_model") {
      //   $rdf_model = new LibRDF_Model(new LibRDF_Storage());
      // }
    }
    $rdf_parser = new LibRDF_Parser('rdfxml');
    $bundle = _edoweb_bundle_type_from_rdf($remote_id, $rdf_model);
    $entity = entity_create(EDOWEB_ENTITY_TYPE, array(
      'remote_id' => $remote_id,
      'bundle_type' => $bundle,
    ));
    _edoweb_storage_entity_deserialize_rdf($entity, $rdf_model);
    return $entity;
  }

  /*
   * Load the RDF for a linked data resource
   */
  public function getRDF($resource_uri, $loaded_uris = 0) {

    $curie = _edoweb_compact_uri($resource_uri);

    if ('rpb:' == substr($curie, 0, 4) || 'nwbib:' == substr($curie, 0, 6)) {
      $rdf_parser = new LibRDF_Parser('turtle');
    } else {
      $rdf_parser = new LibRDF_Parser('rdfxml');
    }
    $rdf_model = new LibRDF_Model(new LibRDF_Storage());

    // Prevent recursion
    if ($loaded_uris > 1) {
      return $rdf_model;
    }

    $http_options = array(
      'method' => 'GET',
      'headers' => array('Accept' => 'application/rdf+xml'),
    );

    $http_response = _edoweb_http_request($resource_uri, $http_options);
    if ((200 == $http_response->code) && isset($http_response->data)) {
      if ($http_response->headers['content-type'] == 'application/json') {
        $json_ld = json_decode($http_response->data);
        $options = array(
          'format' => 'application/ntriples',
        );
        try {
          $rdf_data = _edoweb_escape_utf8(
            jsonld_to_rdf($json_ld, $options)
          );
          $rdf_parser = new LibRDF_Parser('ntriples');
        } catch(JsonLdException $e) {
          watchdog('edoweb', 'Unable to parse JSON LD');
          return FALSE;
        }
      } else {
        $rdf_data = $http_response->data;
      }
    } else {
      return false;
    }

    if (function_exists ('normalizer_normalize')) {
      $rdf_data = normalizer_normalize($rdf_data);
    } else {
      console_log('Normalizer missing, please install PECL intl > 1.0.0');
    }

    try {
      $rdf_model->loadStatementsFromString($rdf_parser, $rdf_data);
    } catch (LibRDF_Error $e) {
      watchdog('edoweb', 'Unable to retrieve Linked Data for @uri',
        array('@uri' => $resource_uri)
      );
    }

    if ('dnb:' == substr(_edoweb_compact_uri($resource_uri), 0, 4)) {
      $ontology_model = new LibRDF_Model(new LibRDF_Storage());
      $ontology_model->loadStatementsFromUri(
        $rdf_parser, 'file://' . __DIR__ . '/gnd.rdf'
      );
      $rdfs_reasoner = new LibRDF_Reasoner('rdfs');
      $rdf_model->inferStatements($rdfs_reasoner, $ontology_model);
    }

    // Workaround for dct:hasVesion in DDC
    if ('ddc:' == substr(_edoweb_compact_uri($resource_uri), 0, 4)) {
      $version_uri = new LibRDF_URINode($resource_uri . '2009/03/about.de');
      $version_stmts = $rdf_model->findStatements(
        $version_uri, null, null
      );
      foreach ($version_stmts as $version_stmt) {
        $rdf_model->addStatement(
          new LibRDF_Statement(
            new LibRDF_URINode($resource_uri),
            $version_stmt->getPredicate(),
            $version_stmt->getObject()
          )
        );
      }
    }

    $OWL = new LibRDF_NS('http://www.w3.org/2002/07/owl#');
    $equivalent_resource_uris = $rdf_model->getTargets(
      new LibRDF_URINode($resource_uri), $OWL->sameAs
    );
    foreach ($equivalent_resource_uris as $equivalent_resource_uri) {
      // For now, only follow lobid-internal redirects to prevent
      // performance issues and non-conformant RDF.
      $equivalent_resource_uri_ns
        = $equivalent_resource_uri->getNamespace()->getValue();
      if ('http://lobid.org/resource/' != $equivalent_resource_uri_ns) {
        continue;
      }
      $same_as_rdf = $this->getRDF(
        $equivalent_resource_uri->getValue(), ++$loaded_uris
      );
      $equivalent_resource = $this->load(
        _edoweb_compact_uri($equivalent_resource_uri->getValue()),
        $same_as_rdf
      );
      $equivalent_resource_rdf = _edoweb_storage_entity_serialize_rdf(
        $equivalent_resource, false, array('descriptive')
      );
      foreach ($equivalent_resource_rdf as $stmt) {
        if ($stmt->getSubject()->isEqual($equivalent_resource_uri)) {
          $subject = new LibRDF_URINode($resource_uri);
        } else {
          $subject = $stmt->getSubject();
        }
        $rdf_model->addStatement(new LibRDF_Statement(
          $subject, $stmt->getPredicate(), $stmt->getObject()
        ));
      }
    }

    return $rdf_model;

  }

}

